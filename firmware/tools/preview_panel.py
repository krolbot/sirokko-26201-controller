#!/usr/bin/env python3
"""Build/run the production C++ TFT renderer and make an offline gallery (Pillow; optional ffmpeg)."""
from pathlib import Path
import argparse
import base64
import hashlib
import html
import json
import shutil
import subprocess
import tempfile
from PIL import Image, ImageDraw, ImageFont


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--video', action='store_true')
    parser.add_argument('--approach', action='store_true', help='accelerated 20→26 C measurement-input demo')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = args.output.resolve()
    native = out / 'native'
    native.mkdir(parents=True, exist_ok=True)
    build = root / 'build-host'
    build.mkdir(exist_ok=True)
    binary = build / 'test_panel_render'
    sources = [root / 'main' / name for name in ('panel_ui.cpp', 'panel_canvas.cpp', 'panel_render.cpp')]
    sources += [root / 'tests/test_panel_render.cpp']
    subprocess.run(['g++', '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I', str(root / 'main'), '-I', str(root / 'components/control_core/include'),
                    *map(str, sources), '-o', str(binary)], check=True)
    with tempfile.TemporaryDirectory(prefix='sirokko-panel-preview-') as temp:
        work = Path(temp)
        ppm = work / 'native'
        frames = work / 'frames'
        approach = work / 'approach'
        command = [str(binary), str(ppm)]
        if args.video or args.approach:
            command.append(str(frames))
        if args.approach:
            command.append(str(approach))
        subprocess.run(command, check=True)
        for path in sorted(ppm.glob('*.ppm')):
            with Image.open(path) as image:
                assert image.size == (320, 240)
                image.save(native / (path.stem + '.png'))
        if args.video:
            if not shutil.which('ffmpeg'):
                raise SystemExit('ffmpeg is required only for --video')
            subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-framerate', '25',
                            '-i', str(frames / 'frame-%04d.ppm'), '-vf', 'scale=640:480:flags=neighbor',
                            '-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-crf', '18', '-movflags', '+faststart',
                            str(out / 'TFT_animations.mp4')], check=True)
        if args.approach:
            subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-framerate', '25',
                            '-i', str(approach / 'frame-%04d.ppm'), '-vf', 'scale=640:480:flags=neighbor',
                            '-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-crf', '18', '-movflags', '+faststart',
                            str(out / 'TFT_heat_approach.mp4')], check=True)
            shutil.copyfile(approach / 'samples.csv', out / 'HEAT_APPROACH_SAMPLES.csv')
            storyboard = Image.new('RGB', (700, 860), '#eef1f3')
            labels = ImageDraw.Draw(storyboard)
            font = ImageFont.truetype(str(root / 'assets/Roboto.ttf'), 15)
            for n, index in enumerate((0, 85, 145, 205, 265, 370)):
                x, y = 20 + n % 2 * 340, 16 + n // 2 * 280
                labels.text((x, y), f'{index / 25:.1f} с — тестовые показания', fill='#292929', font=font)
                with Image.open(approach / f'frame-{index:04d}.ppm') as image:
                    assert image.size == (320, 240)
                    storyboard.paste(image, (x, y + 24))
            storyboard.save(out / 'TFT_heat_approach.png')
    ids = [p.stem for p in sorted(native.glob('*.png'))]
    assert len(ids) == 36
    font = ImageFont.truetype(str(root / 'assets/Roboto.ttf'), 15)
    overview = ['idle', 'heat', 'edit', 'menu', 'prepurge', 'cooldown']
    def sheet(selected, target):
        image = Image.new('RGB', (700, 860), '#eef1f3')
        draw = ImageDraw.Draw(image)
        for index, name in enumerate(selected):
            x = 20 + (index % 2) * 340
            y = 16 + (index // 2) * 280
            draw.text((x, y), name, fill='#292929', font=font)
            with Image.open(native / f'{name}.png') as source:
                image.paste(source, (x, y + 24))
        image.save(target)
    sheet(overview, out / 'TFT_screens.png')
    for index in range(0, len(ids), 6):
        sheet(ids[index:index + 6], out / f'review-{index // 6 + 1}.png')
    cards = []
    for name in ids:
        encoded = base64.b64encode((native / f'{name}.png').read_bytes()).decode('ascii')
        cards.append(f'<figure><figcaption>{html.escape(name)}</figcaption>'
                     f'<img width="320" height="240" alt="{html.escape(name)}" src="data:image/png;base64,{encoded}"></figure>')
    page = '''<!doctype html><html lang="ru"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Панель — кадры прошивки</title><style>body{font:16px system-ui;margin:20px;background:#eef1f3;color:#292929}main{display:flex;gap:20px;flex-wrap:wrap}figure{margin:0}figcaption{padding:8px 0}img{max-width:100%;height:auto;image-rendering:pixelated}p{max-width:850px}</style>
<h1>Кадры настоящего C++ renderer</h1><p>Демонстрационные значения, не показания отопителя. Это тот же код отрисовки, что в прошивке ESP32-S3; галерея не эмулирует контроллер, дисплей или органы управления. Видео использует ускоренные демонстрационные фазы; частота кадров видео не является измерением скорости TFT.</p><main>'''
    (out / 'TFT_gallery.html').write_text(page + ''.join(cards) + '</main></html>', encoding='utf-8')
    manifest = {str(path.relative_to(out)): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in sorted(out.rglob('*')) if path.is_file() and path.suffix in ('.png', '.html', '.mp4', '.csv')}
    (out / 'PREVIEW_MANIFEST.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f'PASS: {len(ids)} production-renderer PNG, offline gallery, review sheets; video={args.video}; approach={args.approach}; output={out}')


if __name__ == '__main__':
    main()
