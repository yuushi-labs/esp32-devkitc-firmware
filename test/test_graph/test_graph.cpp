#include <unity.h>

#include <cstring>

#include "graphics/mini_graph.h"
#include "sensors/comfort.h"
#include "station/alerts.h"

void setUp() {}
void tearDown() {}

namespace {

using namespace fw;

void test_graph_buffer_keeps_the_newest_samples() {
  GraphBuffer graph(3);
  TEST_ASSERT_EQUAL_UINT32(3, static_cast<uint32_t>(graph.capacity()));
  TEST_ASSERT_TRUE(graph.empty());

  graph.push(1.0f);
  graph.push(2.0f);
  graph.push(3.0f);
  TEST_ASSERT_EQUAL_UINT32(3, static_cast<uint32_t>(graph.count()));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, graph.oldest());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.0f, graph.newest());

  graph.push(4.0f);
  TEST_ASSERT_EQUAL_UINT32(3, static_cast<uint32_t>(graph.count()));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, graph.at(0));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.0f, graph.newest());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, graph.minimum());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.0f, graph.maximum());

  graph.clear();
  TEST_ASSERT_TRUE(graph.empty());
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(graph.count()));
}

void test_graph_buffer_clamps_capacity() {
  GraphBuffer zero(0);
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(zero.capacity()));
  GraphBuffer huge(1000);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(GraphBuffer::kMaxCapacity),
                           static_cast<uint32_t>(huge.capacity()));
}

void test_flat_series_draws_a_middle_line() {
  GraphBuffer graph(8);
  for (int index = 0; index < 5; ++index) {
    graph.push(5.0f);
  }
  Framebuffer framebuffer;
  drawMiniGraph(framebuffer, graph, 0, 0, 50, 20);

  TEST_ASSERT_TRUE(framebuffer.pixel(0, 0));    // frame
  TEST_ASSERT_TRUE(framebuffer.pixel(25, 10));  // middle row
  TEST_ASSERT_FALSE(framebuffer.pixel(25, 3));
}

void test_rising_series_spans_bottom_to_top() {
  GraphBuffer graph(8);
  for (int index = 0; index < 8; ++index) {
    graph.push(static_cast<float>(index));
  }
  Framebuffer framebuffer;
  drawMiniGraph(framebuffer, graph, 0, 0, 50, 20);

  TEST_ASSERT_TRUE(framebuffer.pixel(1, 18));
  TEST_ASSERT_TRUE(framebuffer.pixel(48, 1));
}

void test_empty_graph_draws_only_the_frame() {
  GraphBuffer graph(8);
  Framebuffer framebuffer;
  drawMiniGraph(framebuffer, graph, 0, 0, 40, 20);

  TEST_ASSERT_TRUE(framebuffer.pixel(0, 0));
  TEST_ASSERT_TRUE(framebuffer.pixel(39, 19));
  TEST_ASSERT_FALSE(framebuffer.pixel(20, 10));
}

void test_sparkline_has_no_frame() {
  GraphBuffer graph(8);
  Framebuffer framebuffer;
  drawSparkline(framebuffer, graph, 0, 0, 20, 10);

  size_t dark = 0;
  for (int y = 0; y < 10; ++y) {
    for (int x = 0; x < 20; ++x) {
      dark += framebuffer.pixel(x, y) ? 1 : 0;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(dark));
}

void test_comfort_index_and_labels() {
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 69.2f, discomfortIndex(23.4f, 45.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 71.8f, discomfortIndex(25.0f, 50.0f));

  TEST_ASSERT_EQUAL_STRING("cold", comfortLabel(50.0f));
  TEST_ASSERT_EQUAL_STRING("cool", comfortLabel(58.0f));
  TEST_ASSERT_EQUAL_STRING("comfortable", comfortLabel(62.0f));
  TEST_ASSERT_EQUAL_STRING("warm", comfortLabel(68.0f));
  TEST_ASSERT_EQUAL_STRING("hot", comfortLabel(72.0f));
  TEST_ASSERT_EQUAL_STRING("very hot", comfortLabel(80.0f));

  TEST_ASSERT_EQUAL_STRING("sunny", weatherTrendLabel(1));
  TEST_ASSERT_EQUAL_STRING("cloudy", weatherTrendLabel(0));
  TEST_ASSERT_EQUAL_STRING("rain", weatherTrendLabel(-1));
}

void test_alerts_use_the_configured_thresholds() {
  SensorSnapshot sensors;
  sensors.temperatureValid = true;
  sensors.temperatureC = 29.9f;
  sensors.humidityValid = true;
  sensors.humidityPct = 40.0f;

  AlertState alerts = evaluateAlerts(sensors, 0.0f);
  TEST_ASSERT_FALSE(alerts.any());

  sensors.temperatureC = 30.0f;
  alerts = evaluateAlerts(sensors, 0.0f);
  TEST_ASSERT_TRUE(alerts.highTemperature);
  TEST_ASSERT_FALSE(alerts.lowHumidity);

  sensors.temperatureC = 20.0f;
  sensors.humidityPct = 35.0f;
  alerts = evaluateAlerts(sensors, 0.0f);
  TEST_ASSERT_FALSE(alerts.highTemperature);
  TEST_ASSERT_TRUE(alerts.lowHumidity);

  // The pressure alert only fires while the sensor still answers.
  sensors.pressureValid = true;
  sensors.pressureHpa = 1006.0f;
  alerts = evaluateAlerts(sensors, -1.5f);
  TEST_ASSERT_TRUE(alerts.pressureJump);
  alerts = evaluateAlerts(sensors, 1.4f);
  TEST_ASSERT_FALSE(alerts.pressureJump);

  sensors.pressureValid = false;
  TEST_ASSERT_FALSE(evaluateAlerts(sensors, -3.0f).pressureJump);

  // A missing temperature must not fire the high temperature alert.
  SensorSnapshot empty;
  TEST_ASSERT_FALSE(evaluateAlerts(empty, 0.0f).any());
}

void test_alert_line_formatting() {
  SensorSnapshot sensors;
  sensors.temperatureValid = true;
  sensors.temperatureC = 31.0f;
  sensors.humidityValid = true;
  sensors.humidityPct = 30.0f;
  sensors.pressureValid = true;
  sensors.pressureHpa = 1002.0f;

  AlertState alerts;
  char out[48];
  formatAlertLine(alerts, sensors, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("none", out);

  alerts = evaluateAlerts(sensors, 2.0f);
  formatAlertLine(alerts, sensors, out, sizeof(out));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "HOT 31.0C"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "DRY 30%"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "P JUMP"));

  char tiny[6];
  const size_t needed = formatAlertLine(alerts, sensors, tiny, sizeof(tiny));
  TEST_ASSERT_TRUE(needed > sizeof(tiny) - 1);
  TEST_ASSERT_EQUAL_UINT32(sizeof(tiny) - 1, static_cast<uint32_t>(std::strlen(tiny)));
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_graph_buffer_keeps_the_newest_samples);
  RUN_TEST(test_graph_buffer_clamps_capacity);
  RUN_TEST(test_flat_series_draws_a_middle_line);
  RUN_TEST(test_rising_series_spans_bottom_to_top);
  RUN_TEST(test_empty_graph_draws_only_the_frame);
  RUN_TEST(test_sparkline_has_no_frame);
  RUN_TEST(test_comfort_index_and_labels);
  RUN_TEST(test_alerts_use_the_configured_thresholds);
  RUN_TEST(test_alert_line_formatting);
  return UNITY_END();
}
