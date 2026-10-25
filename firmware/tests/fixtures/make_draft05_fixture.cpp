#define main original_suite_entry
#include "sim_tests.cpp"
#undef main
#include <fstream>
int main(int argc,char** argv) {
    if(argc!=2) return 2;
    fake_idf::reset(); CommissioningStore store;
    const auto c=test_config();
    check(c.schema_version==3 && validate_config(c)==CONFIG_OK,"valid baseline fixture");
    check(store.save_config(c),"baseline NVS serialization");
    bool ok{}; const auto read=store.load_config(&ok);
    check(ok && read.commissioning_accepted,"baseline accepted NVS readback");
    const auto& data=fake_idf::nvs.at("config");
    std::ofstream output(argv[1],std::ios::binary); output.write(reinterpret_cast<const char*>(data.data()),data.size());
    return output.good()?0:1;
}
