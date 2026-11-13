#!/usr/bin/env python3
"""Prepare an offline envelope_v2 blob using the target ABI. Never writes hardware.

Input numbers must come from qualification of the actual assembly, not nominal
catalog values. This tool validates data/encoding, not physical qualification.
Run in the project's ESP-IDF toolchain environment (host g++ is also required).
"""
import argparse
import hashlib
import json
import pathlib
import shutil
import struct
import subprocess
import tempfile

FIRMWARE = pathlib.Path(__file__).resolve().parents[1]
TIMES = ('recovery_purge_ms', 'prepurge_ms', 'preheat_ms', 'ignition_ms', 'verify_ms', 'postpurge_ms')
TEMPERATURES = ('cooling_exhaust_milli_c',
                'maximum_exhaust_milli_c', 'maximum_air_milli_c', 'minimum_ignition_rise_milli_c',
                'minimum_run_exhaust_milli_c', 'minimum_ignition_air_rise_milli_c')
CODES = ('purge_speed_code', 'reference_speed_code', 'exploration_floor_code')
FIELDS = TIMES + TEMPERATURES + CODES


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f'duplicate JSON key: {key}')
        result[key] = value
    return result


def read_spec(path):
    spec = json.loads(path.read_text(), object_pairs_hook=unique_object)
    if not isinstance(spec, dict) or set(spec) != {'qualification_reference', 'envelope'}:
        raise ValueError('expected exactly qualification_reference and envelope')
    evidence = spec['qualification_reference']
    if not isinstance(evidence, str) or not evidence.strip():
        raise ValueError('qualification_reference must identify the external qualification record')
    envelope = spec['envelope']
    if not isinstance(envelope, dict) or set(envelope) != set(FIELDS):
        raise ValueError('envelope must specify every field exactly; defaults and provisioned flags are not accepted')
    for key, value in envelope.items():
        low, high = ((0, 2**64-1) if key in TIMES else
                     (0, 2**16-1) if key in CODES else (-2**31, 2**31-1))
        if type(value) is not int or not low <= value <= high:
            raise ValueError(f'{key}: expected an integer in [{low}, {high}]')
    return spec


def run(args):
    subprocess.run([str(arg) for arg in args], check=True, timeout=60)


def make_blob(spec, directory, target_cxx, objcopy, host_cxx):
    """Host semantic validation plus target-produced record bytes and offsets."""
    assignments = '\n'.join(f'    e.{key} = {value}{"ULL" if key in TIMES else ""};'
                            for key, value in spec['envelope'].items())
    common = ('#include "sirokko/control.hpp"\n#include "record_codec.hpp"\n'
              'constexpr sirokko::SafetyEnvelope make_envelope() {\n'
              '    sirokko::SafetyEnvelope e{}; e.provisioned = true;\n' + assignments + '\n    return e;\n}\n')
    includes = ['-I', FIRMWARE/'components/control_core/include', '-I', FIRMWARE/'main']
    validator = directory/'validate.cpp'
    validator.write_text(common + 'int main() { return sirokko::valid_safety_envelope(make_envelope()) ? 0 : 2; }\n')
    executable = directory/'validate'
    run([host_cxx, '-std=c++20', *includes, validator, FIRMWARE/'components/control_core/profile.cpp', '-o', executable])
    run([executable])
    source = directory/'record.cpp'
    source.write_text(common + '''
using EnvelopeRecord = record_codec::Record<sirokko::SafetyEnvelope>;
constexpr EnvelopeRecord make_record() {
    EnvelopeRecord r{};
    r.magic = 0x454E5632;
    r.version = 2;
    r.payload_size = sizeof(sirokko::SafetyEnvelope);
    r.value = make_envelope();
    return r;
}
extern "C" {
__attribute__((used,section(".factory_record"))) const EnvelopeRecord factory_record = make_record();
__attribute__((used,section(".factory_layout"))) const uint32_t factory_layout[] = {
    sizeof(EnvelopeRecord), offsetof(EnvelopeRecord, checksum), sizeof(sirokko::SafetyEnvelope)
};
}
''')
    obj = directory/'record.o'
    run([target_cxx, '-std=c++20', '-fno-exceptions', '-fno-rtti', *includes, '-c', source, '-o', obj])
    raw, layout_path = directory/'raw.bin', directory/'layout.bin'
    run([objcopy, '-O', 'binary', '--only-section=.factory_record', obj, raw])
    run([objcopy, '-O', 'binary', '--only-section=.factory_layout', obj, layout_path])
    layout = struct.unpack('<III', layout_path.read_bytes())
    size, checksum_offset, payload_size = layout
    data = bytearray(raw.read_bytes())
    if len(data) != size or checksum_offset + 8 > size:
        raise ValueError('unexpected target record layout')
    if struct.unpack_from('<IHH', data) != (0x454E5632, 2, payload_size):
        raise ValueError('target record header does not match runtime codec')
    checksum = 1469598103934665603
    for byte in data[:checksum_offset]:
        checksum = ((checksum ^ byte) * 1099511628211) & ((1 << 64) - 1)
    struct.pack_into('<Q', data, checksum_offset, checksum)
    return bytes(data), layout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=pathlib.Path, required=True)
    parser.add_argument('--output-dir', type=pathlib.Path, required=True)
    parser.add_argument('--target-cxx', default='xtensa-esp32s3-elf-g++')
    parser.add_argument('--objcopy', default='xtensa-esp32s3-elf-objcopy')
    parser.add_argument('--host-cxx', default='g++')
    args = parser.parse_args()
    spec = read_spec(args.input)
    # A fresh output directory prevents mixing records from different qualifications.
    if args.output_dir.exists():
        raise ValueError('output directory must not already exist')
    for command in (args.target_cxx, args.objcopy, args.host_cxx):
        if not shutil.which(command):
            raise ValueError(f'required compiler/tool unavailable: {command}')
    with tempfile.TemporaryDirectory(prefix='sirokko-factory-') as temporary:
        data, layout = make_blob(spec, pathlib.Path(temporary), args.target_cxx, args.objcopy, args.host_cxx)
    args.output_dir.mkdir(parents=True)
    blob = args.output_dir/'envelope_v2.bin'
    blob.write_bytes(data)
    # Input for the official ESP-IDF NVS partition generator, NOT a flash command.
    (args.output_dir/'envelope.csv').write_text(
        'key,type,encoding,value\nsirokko,namespace,,\nenvelope_v2,file,binary,envelope_v2.bin\n')
    manifest = {'qualification_reference': spec['qualification_reference'], 'input': spec['envelope'],
                'nvs_namespace': 'sirokko', 'nvs_key': 'envelope_v2', 'magic': '0x454E5632',
                'record_version': 2, 'record_bytes': layout[0], 'checksum_offset': layout[1],
                'payload_bytes': layout[2], 'sha256': hashlib.sha256(data).hexdigest(),
                'physical_qualification_verified_by_tool': False,
                'warning': 'Do not overwrite a populated NVS partition: settings and credentials may be lost.'}
    (args.output_dir/'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n')
    print(f'Prepared {blob}; no device was written; physical qualification is external.')


if __name__ == '__main__':
    main()
