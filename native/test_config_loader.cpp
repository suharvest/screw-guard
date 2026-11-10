#include "app_config.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <unistd.h>
using namespace screw_guard;
static void check(bool v, const char* m) { if (!v) { std::cerr << "FAIL " << m << '\n'; std::exit(1); } }
int main() {
    const std::string p = "/tmp/screw-guard-values.json";
    const std::string profile = "{\"revision\":1,\"expected_blocks\":1,\"expected_screws_per_block\":1,\"canonical_size\":[300,200],\"screw_rois\":[{\"id\":1,\"norm_x\":0.5,\"norm_y\":0.5,\"norm_r\":0.05}]}";
    std::ofstream(p) << "{\"values\":{\"profile_json\":" << std::quoted(profile) << ",\"confidence_threshold\":0.6,\"max_blocks\":4}}";
    AppConfig c; std::string code, msg; check(ConfigValidator::load_file(p, c, code, msg), "wrapper load"); check(c.profile.revision == 1, "revision");
    std::ofstream(p) << "{\"format\":\"screw-guard-calib/1\",\"profile\":" << profile << ",\"confidence_threshold\":0.6,\"max_blocks\":4}";
    check(ConfigValidator::load_file(p, c, code, msg), "web export load");
    const std::string no_revision = "{\"expected_blocks\":1,\"expected_screws_per_block\":1,\"canonical_size\":[300,200],\"screw_rois\":[{\"id\":1,\"norm_x\":0.5,\"norm_y\":0.5,\"norm_r\":0.05}]}";
    std::ofstream(p) << "{\"profile_json\":" << std::quoted(no_revision) << ",\"confidence_threshold\":0.6,\"max_blocks\":4}";
    check(!ConfigValidator::load_file(p, c, code, msg) && code == "MISSING_FIELD", "revision required");
    std::ofstream(p) << "{\"profile_json\":123,\"confidence_threshold\":0.6,\"max_blocks\":4}";
    check(!ConfigValidator::load_file(p, c, code, msg) && code == "INVALID_FIELD_TYPE", "type rejection");
    std::ofstream(p) << "{\"confidence_threshold\":0.6,\"max_blocks\":4}";
    check(!ConfigValidator::load_file(p, c, code, msg) && code == "MISSING_FIELD", "missing rejection");
    std::string huge = profile; huge.insert(1, "\"part_name\":\"" + std::string(4100, 'x') + "\",");
    std::ofstream(p) << "{\"profile_json\":" << std::quoted(huge) << ",\"confidence_threshold\":0.6,\"max_blocks\":4}";
    check(!ConfigValidator::load_file(p, c, code, msg) && code == "PROFILE_TOO_LARGE", "profile size rejection");
    std::string thirty_three = profile;
    const std::string one_roi = "{\"id\":1,\"norm_x\":0.5,\"norm_y\":0.5,\"norm_r\":0.02}";
    thirty_three = "{\"revision\":1,\"expected_blocks\":1,\"expected_screws_per_block\":33,\"canonical_size\":[300,200],\"screw_rois\":[";
    for (int i = 0; i < 33; ++i) { if (i) thirty_three += ","; thirty_three += one_roi; } thirty_three += "]}";
    std::ofstream(p) << "{\"profile_json\":" << std::quoted(thirty_three) << ",\"confidence_threshold\":0.6,\"max_blocks\":4}";
    check(!ConfigValidator::load_file(p, c, code, msg) && code == "INVALID_PROFILE_SCREWS_COUNT", "33 screws rejection");
    std::ofstream(p) << "{\"profile_json\":" << std::quoted(profile) << ",\"confidence_threshold\":0.6,\"max_blocks\":18446744073709551615}";
    check(!ConfigValidator::load_file(p, c, code, msg) && code == "INVALID_MAX_BLOCKS", "uint64 max_blocks rejection");
    std::ofstream(p) << "{\"profile_json\":" << std::quoted(profile) << ",\"confidence_threshold\":0.6,\"max_blocks\":4294967300}";
    check(!ConfigValidator::load_file(p, c, code, msg) && code == "INVALID_MAX_BLOCKS", "wrapped int32 max_blocks rejection");
    for (const std::string value : {"4294967297", "18446744073709551615"}) {
        std::string overflow = profile;
        auto offset = overflow.find("\"expected_screws_per_block\":1");
        overflow.replace(offset, std::string("\"expected_screws_per_block\":1").size(), "\"expected_screws_per_block\":" + value);
        std::ofstream(p) << "{\"profile_json\":" << std::quoted(overflow) << ",\"confidence_threshold\":0.6,\"max_blocks\":4}";
        check(!ConfigValidator::load_file(p, c, code, msg) && code == "INVALID_PROFILE_SCREWS_MISMATCH", "expected screws overflow rejection");
    }
    unlink(p.c_str()); std::cout << "config loader tests passed\n"; return 0;
}
