// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../ui/xui/asset-browser-decode.hh"
#include <glib.h>
#include <cstring>
#include <limits>

using namespace xemu::asset_browser;

template <typename T>
static capture::SharedCaptureBlock Block(const std::vector<T> &values)
{
    auto block = std::make_shared<capture::CaptureImmutableBlock>();
    block->bytes.resize(values.size() * sizeof(T));
    std::memcpy(block->bytes.data(), values.data(), block->bytes.size());
    return block;
}
static std::shared_ptr<capture::CaptureOccurrence>
Draw(uint64_t id, uint32_t backend = 2, uint32_t primitive = 5)
{
    auto event = std::make_shared<capture::CaptureOccurrence>();
    event->event_id = id;
    event->summary.key = { 1, 2, 10, uint32_t(id), id };
    event->summary.scope.title_id = 17;
    event->summary.primitive_mode = primitive;
    event->summary.shader_count = 1;
    event->summary.shaders[0].stage = capture::Stage::Pixel;
    event->summary.shaders[0].hash.bytes[0] = 42;
    event->pending = false;
    event->finished = event->emitted = true;
    event->inputs.complete = true;
    capture::CaptureOwnedBlob blob;
    blob.name = "vertex.attribute0";
    blob.format = backend == 2 ? 106 : 0x1406;
    blob.components = 3;
    blob.stride = 12;
    blob.count = 3;
    blob.data = Block<float>({ 0, 0, 0, 2, 0, 0, 0, 2, 0 });
    event->inputs.blobs.push_back(blob);
    event->inputs.registers.push_back(
        { backend == 2 ? "capture.vertices.first" : "capture.first_vertex",
          0 });
    event->inputs.registers.push_back(
        { backend == 2 ? "capture.vertices.count" : "capture.last_vertex",
          backend == 2 ? 3U : 2U });
    event->inputs.registers.push_back(
        { backend == 2 ? "capture.vertex.0.enabled" : "vertex.enabled0", 1 });
    return event;
}
static capture::CaptureSessionSnapshot Snapshot()
{
    capture::CaptureSessionSnapshot s;
    s.state = capture::CaptureSessionState::Ready;
    s.frame_window_complete = true;
    s.context.backend = 2;
    s.context.session_epoch = 1;
    s.context.renderer_epoch = 2;
    s.context.scope.title_id = 17;
    return s;
}
static void TestManyUses()
{
    auto snapshot = Snapshot();
    for (uint64_t i = 1; i <= 32; ++i)
        snapshot.events.push_back(Draw(i));
    auto catalog = BuildAssetCatalog(snapshot);
    g_assert_cmpuint(catalog.entries.size(), ==, 32);
    g_assert_cmpuint(catalog.matching_draws, ==, 32);
    g_assert_true(catalog.complete_frame);
    for (size_t i = 0; i < 32; ++i)
        g_assert_cmpuint(catalog.entries[i].parts.front()->id, ==, i + 1);
    auto car = MakeAssetAssembly(catalog, { 1, 2, 3, 2 }, "My car");
    g_assert_cmpuint(car.parts.size(), ==, 3);
    g_assert_cmpstr(car.label.c_str(), ==, "My car");
    g_assert_true(car.user_confirmed && car.complete);
    g_assert_true(
        MakeAssetAssembly(catalog, { 1, 999 }, "Missing").parts.empty());
}
static void TestSparseBounds()
{
    auto event = Draw(1);
    auto &position = event->inputs.blobs[0];
    position.count = 4;
    position.data = Block<float>({ 0, 0, 0, 2, 0, 0, 0, 2, 0, 1000000, 0, 0 });
    capture::CaptureOwnedBlob indices;
    indices.name = "vertex.indices";
    indices.count = 3;
    indices.data = Block<uint32_t>({ 0, 1, 2 });
    event->inputs.blobs.push_back(indices);
    auto part = DecodeAssetPart(event, 2);
    g_assert_true(part.status == AssetStatus::Ready);
    g_assert_cmpuint(part.vertices.size(), ==, 3);
    g_assert_true(part.source_vertices == std::vector<uint32_t>({ 0, 1, 2 }));
    g_assert_cmpfloat(part.bounds.maximum[0], ==, 2);
    g_assert_cmpfloat(part.bounds.maximum[1], ==, 2);
}
static void TestSubdraws()
{
    auto event = Draw(1, 1);
    event->inputs.blobs[0].count = 6;
    event->inputs.blobs[0].data = Block<float>(
        { 0, 0, 0, 1, 0, 0, 0, 1, 0, 99, 99, 99, 2, 2, 2, 3, 3, 3 });
    capture::CaptureOwnedBlob starts, counts;
    starts.name = "geometry.draw_starts";
    counts.name = "geometry.draw_counts";
    starts.count = counts.count = 2;
    starts.data = Block<uint32_t>({ 0, 4 });
    counts.data = Block<uint32_t>({ 4, 2 });
    event->inputs.blobs.push_back(starts);
    event->inputs.blobs.push_back(counts);
    auto part = DecodeAssetPart(event, 1);
    g_assert_true(part.status == AssetStatus::Ready);
    g_assert_cmpuint(part.indices.size(), ==, 3);
    g_assert_cmpuint(part.vertices.size(), ==, 3);
    g_assert_cmpfloat(part.bounds.maximum[0], ==, 1);
}
static void TestFormats()
{
    capture::CaptureOwnedBlob blob;
    blob.count = 1;
    blob.components = 4;
    blob.stride = 4;
    blob.format = 37; // VK_FORMAT_R8G8B8A8_UNORM
    blob.data = Block<uint8_t>({ 0, 255, 128, 255 });
    std::array<float, 4> value;
    g_assert_true(DecodeAssetAttribute(blob, 2, 0, &value));
    g_assert_cmpfloat(value[0], ==, 0);
    g_assert_cmpfloat(value[1], ==, 1);
    g_assert_cmpfloat_with_epsilon(value[2], 128.0f / 255, 0.00001f);
    blob.format = 0x1401; // GL_UNSIGNED_BYTE
    blob.normalized = 1;
    g_assert_true(DecodeAssetAttribute(blob, 1, 0, &value));
    g_assert_cmpfloat(value[1], ==, 1);
    blob.format = 78; // VK_FORMAT_R16G16_SNORM
    blob.components = 2;
    blob.data = Block<int16_t>({ -32768, 32767 });
    g_assert_true(DecodeAssetAttribute(blob, 2, 0, &value));
    g_assert_cmpfloat(value[0], ==, -1);
    g_assert_cmpfloat(value[1], ==, 1);
    g_assert_false(DecodeAssetAttribute(blob, 2, 1, &value));
}
static void TestIntegerAttributes()
{
    capture::CaptureOwnedBlob blob;
    blob.format = 99; // VK_FORMAT_R32_SINT, compressed normal bits.
    blob.components = 1;
    blob.stride = 4;
    blob.count = 2;
    blob.data = Block<uint32_t>({ 0x81234567, 0x01234567 });
    std::array<uint32_t, 4> values;
    bool signed_values = false;
    g_assert_true(
        DecodeAssetIntegerAttribute(blob, 2, 0, &values, &signed_values));
    g_assert_true(signed_values);
    g_assert_cmphex(values[0], ==, 0x81234567);
    g_assert_cmpuint(values[3], ==, 1);
    g_assert_true(
        DecodeAssetIntegerAttribute(blob, 2, 1, &values, &signed_values));
    g_assert_cmphex(values[0], ==, 0x01234567);
    g_assert_false(
        DecodeAssetIntegerAttribute(blob, 2, 2, &values, &signed_values));
    blob.format = 0x1404;
    blob.integer = 1;
    g_assert_true(
        DecodeAssetIntegerAttribute(blob, 1, 0, &values, &signed_values));
    g_assert_cmphex(values[0], ==, 0x81234567);
    blob.normalized = 1;
    g_assert_false(
        DecodeAssetIntegerAttribute(blob, 1, 0, &values, &signed_values));
}
static void TestInvalid()
{
    auto event = Draw(1);
    event->inputs.blobs[0].data = Block<float>(
        { std::numeric_limits<float>::quiet_NaN(), 0, 0, 1, 0, 0, 0, 1, 0 });
    auto part = DecodeAssetPart(event, 2);
    g_assert_true(part.status == AssetStatus::Malformed);
    g_assert_true(part.occurrence == event);
    g_assert_true(part.vertices.empty());
    event = Draw(2);
    capture::CaptureOwnedBlob indices;
    indices.name = "vertex.indices";
    indices.count = 3;
    indices.data = Block<uint32_t>({ 0, 1, 999 });
    event->inputs.blobs.push_back(indices);
    g_assert_true(DecodeAssetPart(event, 2).status == AssetStatus::Malformed);
    event = Draw(3);
    event->pending = true;
    g_assert_true(DecodeAssetPart(event, 2).status == AssetStatus::Pending);
}
static void TestLargeAndBudget()
{
    auto event = Draw(1);
    std::vector<float> positions;
    for (size_t i = 0; i < 6000; ++i) {
        positions.push_back(float(i));
        positions.push_back(float(i % 3));
        positions.push_back(0);
    }
    event->inputs.blobs[0].data = Block(positions);
    event->inputs.blobs[0].count = 6000;
    event->inputs.registers[1].value = 6000;
    auto part = DecodeAssetPart(event, 2);
    g_assert_true(part.status == AssetStatus::Ready);
    g_assert_cmpuint(part.vertices.size(), ==, 6000);
    AssetLimits limits;
    limits.maximum_vertices = 4096;
    g_assert_true(DecodeAssetPart(event, 2, limits).status ==
                  AssetStatus::BudgetExceeded);
    auto snapshot = Snapshot();
    snapshot.events = { Draw(1), Draw(2) };
    limits.maximum_parts = 1;
    auto catalog = BuildAssetCatalog(snapshot, limits);
    g_assert_true(catalog.budget_exceeded);
    g_assert_false(catalog.complete_frame);
}
static void TestHostTopology()
{
    auto event = Draw(1, 2, 8); // Guest quads, already expanded host triangles.
    capture::CaptureOwnedBlob assembly;
    assembly.name = "vk.pipeline.assembly";
    assembly.data = Block<uint32_t>({ 20, 0, 0, 0, 0, 3, 0, 0 });
    event->inputs.blobs.push_back(assembly);
    auto part = DecodeAssetPart(event, 2);
    g_assert_true(part.status == AssetStatus::Ready);
    g_assert_cmpuint(part.indices.size(), ==, 3);
}
static void TestPartialFrame()
{
    auto snapshot = Snapshot();
    snapshot.state = capture::CaptureSessionState::BudgetExceeded;
    snapshot.events = { Draw(1) };
    auto catalog = BuildAssetCatalog(snapshot);
    g_assert_cmpuint(catalog.parts.size(), ==, 1);
    g_assert_false(catalog.complete_frame);
    g_assert_false(catalog.entries[0].complete);
}
static void TestStripAndAttributes()
{
    auto event = Draw(1, 2, 6);
    event->inputs.blobs[0].count = 4;
    event->inputs.blobs[0].data =
        Block<float>({ 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0 });
    event->inputs.registers[1].value = 4;
    capture::CaptureOwnedBlob assembly, uv;
    assembly.name = "vk.pipeline.assembly";
    assembly.data = Block<uint32_t>({ 20, 0, 0, 0, 0, 4, 0, 0 });
    uv.name = "vertex.current9";
    uv.format = 103;
    uv.components = 2;
    uv.count = 1;
    uv.stride = 8;
    uv.data = Block<float>({ 0.25f, 0.75f });
    event->inputs.blobs.push_back(assembly);
    event->inputs.blobs.push_back(uv);
    auto part = DecodeAssetPart(event, 2);
    g_assert_true(part.status == AssetStatus::Ready);
    const uint32_t wanted[] = { 0, 1, 2, 2, 1, 3 };
    g_assert_cmpmem(part.indices.data(), part.indices.size() * 4, wanted,
                    sizeof(wanted));
    g_assert_true(part.has_uv);
    for (const auto &v : part.vertices) {
        g_assert_cmpfloat(v.uv[0], ==, 0.25f);
        g_assert_cmpfloat(v.uv[1], ==, 0.75f);
    }
}
static void TestCompressedAndHalf()
{
    capture::CaptureOwnedBlob blob;
    blob.count = 1;
    blob.components = 2;
    blob.format = 83;
    blob.stride = 4;
    blob.data = Block<uint16_t>({ 0x3c00, 0xc000 });
    std::array<float, 4> value;
    g_assert_true(DecodeAssetAttribute(blob, 2, 0, &value));
    g_assert_cmpfloat(value[0], ==, 1);
    g_assert_cmpfloat(value[1], ==, -2);
    auto event = Draw(1);
    event->inputs.blobs[0].components = 1;
    event->inputs.blobs[0].format = 99;
    event->inputs.blobs[0].stride = 4;
    event->inputs.blobs[0].data = Block<uint32_t>({ 0, 1023, 1023U << 11 });
    event->inputs.registers.push_back(
        { "capture.vertices.compressed_mask", 1 });
    auto part = DecodeAssetPart(event, 2);
    g_assert_true(part.status == AssetStatus::Ready);
    g_assert_cmpfloat(part.bounds.maximum[0], ==, 1);
    g_assert_cmpfloat(part.bounds.maximum[1], ==, 1);
    auto normal = event->inputs.blobs[0];
    normal.name = "vertex.attribute2";
    normal.slot = 2;
    normal.stride = 0;
    normal.count = 3;
    normal.data = Block<uint32_t>({ 511U << 22 });
    event->inputs.blobs.push_back(normal);
    event->inputs.registers.back().value |= 1U << 2;
    part = DecodeAssetPart(event, 2);
    g_assert_true(part.status == AssetStatus::Ready);
    for (const auto &vertex : part.vertices)
        g_assert_cmpfloat(vertex.normal[2], ==, 1);
    event->inputs.blobs[0].format = 0x1404;
    event->inputs.blobs[0].integer = 1;
    event->inputs.blobs[1].format = 0x1406;
    event->inputs.blobs[1].components = 3;
    event->inputs.blobs[1].stride = 12;
    event->inputs.blobs[1].count = 1;
    event->inputs.blobs[1].data = Block<float>({ 0, 0, 1 });
    event->inputs.registers.push_back({ "host.primitive_mode", 4 });
    event->inputs.registers.push_back({ "capture.first_vertex", 0 });
    event->inputs.registers.push_back({ "capture.last_vertex", 2 });
    capture::CaptureOwnedBlob starts, counts;
    starts.name = "geometry.draw_starts";
    counts.name = "geometry.draw_counts";
    starts.count = counts.count = 1;
    starts.data = Block<uint32_t>({ 0 });
    counts.data = Block<uint32_t>({ 3 });
    event->inputs.blobs.push_back(starts);
    event->inputs.blobs.push_back(counts);
    part = DecodeAssetPart(event, 1);
    g_assert_true(part.status == AssetStatus::Ready);
    g_assert_cmpfloat(part.bounds.maximum[0], ==, 1);
    g_assert_cmpfloat(part.bounds.maximum[1], ==, 1);
    for (const auto &vertex : part.vertices)
        g_assert_cmpfloat(vertex.normal[2], ==, 1);
}
static void TestMissingEvidence()
{
    auto event = Draw(1);
    event->summary.index_count = 3;
    g_assert_true(DecodeAssetPart(event, 2).status == AssetStatus::Missing);
    event = Draw(2, 1, 6);
    g_assert_true(DecodeAssetPart(event, 1).status == AssetStatus::Missing);
    event = Draw(3);
    event->limitations |= capture::CaptureReadbackFailed;
    event->failure = "Texture readback failed";
    auto snapshot = Snapshot();
    snapshot.events.push_back(event);
    auto catalog = BuildAssetCatalog(snapshot);
    g_assert_false(catalog.complete_frame);
    g_assert_true(catalog.parts[0]->status == AssetStatus::Missing);
    g_assert_true(catalog.parts[0]->occurrence == event);
}
int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, nullptr);
    g_test_add_func("/asset/catalog/many-uses", TestManyUses);
    g_test_add_func("/asset/decode/sparse-bounds", TestSparseBounds);
    g_test_add_func("/asset/decode/subdraws", TestSubdraws);
    g_test_add_func("/asset/decode/formats", TestFormats);
    g_test_add_func("/asset/decode/integer-bits", TestIntegerAttributes);
    g_test_add_func("/asset/decode/invalid", TestInvalid);
    g_test_add_func("/asset/decode/large-budget", TestLargeAndBudget);
    g_test_add_func("/asset/decode/host-topology", TestHostTopology);
    g_test_add_func("/asset/catalog/partial-frame", TestPartialFrame);
    g_test_add_func("/asset/decode/strip-attributes", TestStripAndAttributes);
    g_test_add_func("/asset/decode/compressed-half", TestCompressedAndHalf);
    g_test_add_func("/asset/decode/missing-evidence", TestMissingEvidence);
    return g_test_run();
}
