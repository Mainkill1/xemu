#include "../../ui/xui/shader-browser-usage-export.hh"

#include <cassert>
#include <string>

using namespace xemu::shader_browser;

int main()
{
    Snapshot snapshot{};
    snapshot.snapshot_sequence = 7;
    snapshot.session_epoch = 2;
    snapshot.current_frame = 100;
    Entry entry{};
    entry.key.stage = Stage::Pixel;
    entry.key.hash.bytes[0] = 0x31;
    entry.draw_count = 12;
    entry.last_frame = 95;
    entry.observed_in_session = true;
    entry.compile_cpu.AddSample(200);
    ShaderScope scope{};
    scope.title_id = 0x12345678;
    entry.scopes.push_back(scope);
    snapshot.entries.push_back(entry);
    Entry absent{};
    absent.key.stage = Stage::Vertex;
    absent.key.hash.bytes[0] = 0x30;
    snapshot.entries.push_back(absent);
    const std::string csv = SerializeUsageCsv(snapshot);
    const std::string json = SerializeUsageJson(snapshot);
    assert(QuoteUsageCsvField("a,\"b\"\n") == "\"a,\"\"b\"\"\n\"");
    assert(csv.find("stage,shader_hash") != std::string::npos);
    assert(csv.find("PS,") != std::string::npos);
    assert(csv.find("VS,") < csv.find("PS,"));
    assert(csv.find(",12,95,5,") != std::string::npos);
    assert(csv.find(",,,,") != std::string::npos);
    assert(json.find("\"schema\":\"xemu.shader-usage.v1\"") != std::string::npos);
    assert(json.find("\"compile_cpu_average_ns\":null") != std::string::npos);
    assert(json.find("\"compile_cpu_average_ns\":200") != std::string::npos);
    return 0;
}
