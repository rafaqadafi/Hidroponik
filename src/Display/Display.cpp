#include "Display.h"
#include <Arduino.h>
#include "Config/config.h"

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

namespace {

class LGFX : public lgfx::LGFX_Device
{
    lgfx::Panel_ILI9488 _panel_instance;
    lgfx::Bus_SPI       _bus_instance;

public:
    LGFX()
    {
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host = SPI2_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = Config::Display::SPI_WRITE_FREQUENCY_HZ;
            cfg.freq_read  = Config::Display::SPI_READ_FREQUENCY_HZ;
            cfg.spi_3wire  = false;
            cfg.use_lock   = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = Config::Pins::LCD_SCLK;
            cfg.pin_mosi = Config::Pins::LCD_MOSI;
            cfg.pin_miso = Config::Pins::LCD_MISO;
            cfg.pin_dc   = Config::Pins::LCD_DC;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs           = Config::Pins::LCD_CS;
            cfg.pin_rst          = Config::Pins::LCD_RST;
            cfg.pin_busy         = -1;
            cfg.memory_width     = 320;
            cfg.memory_height    = 480;
            cfg.panel_width      = 320;
            cfg.panel_height     = 480;
            cfg.offset_x         = 0;
            cfg.offset_y         = 0;
            cfg.offset_rotation  = 0;
            cfg.dummy_read_pixel = 8;
            // RDDPM ILI9488 pada modul ini dibaca tanpa bit dummy tambahan.
            cfg.dummy_read_bits  = 0;
            cfg.readable         = true;
            cfg.invert           = false;
            cfg.rgb_order        = false;
            cfg.dlen_16bit       = false;
            cfg.bus_shared       = false;
            _panel_instance.config(cfg);
        }
        setPanel(&_panel_instance);
    }
};

LGFX lcd;
lgfx::LGFX_Sprite metricSprite(&lcd);
lgfx::LGFX_Sprite flowSprite(&lcd);
lgfx::LGFX_Sprite graphSprite(&lcd);

// Tactical telemetry palette: solid surfaces, hard grid lines, no gradients.
constexpr uint32_t CLR_BG       = 0x0E1419;
constexpr uint32_t CLR_HEADER   = 0x18232B;
constexpr uint32_t CLR_PANEL    = 0x151E25;
constexpr uint32_t CLR_GRID     = 0x2A3942;
constexpr uint32_t CLR_BORDER   = 0x49606B;
constexpr uint32_t CLR_TEXT     = 0xE8EEF2;
constexpr uint32_t CLR_MUTED    = 0x8A9AA5;
constexpr uint32_t CLR_DIM      = 0x5A6A73;
constexpr uint32_t CLR_OK       = 0x5BE37D;
constexpr uint32_t CLR_WARN     = 0xF2C14E;
constexpr uint32_t CLR_BAD      = 0xFF5C5C;

constexpr uint32_t ACC_TEMP     = 0x54B7E8;
constexpr uint32_t ACC_PH       = 0x5BE37D;
constexpr uint32_t ACC_TDS      = 0xB88CFF;
constexpr uint32_t ACC_LEVEL    = 0x50D6D6;
constexpr uint32_t ACC_LIGHT    = 0xF2C14E;
constexpr uint32_t ACC_TURB     = 0xFF8066;
constexpr uint32_t ACC_FLOW     = 0x65A9FF;

constexpr uint8_t PAGE_NUMERIC  = 0;
constexpr uint8_t PAGE_TRENDS   = 1;
constexpr uint8_t HISTORY_POINTS = 48;

constexpr int SCREEN_W = 480;
constexpr int SCREEN_H = 320;
constexpr int PANEL_X[3] = {8, 164, 320};
constexpr int PANEL_W = 152;
constexpr int PANEL_H = 80;
constexpr int PANEL_ROW0_Y = 40;
constexpr int PANEL_ROW1_Y = 126;
constexpr int FLOW_Y = 214;
constexpr int FLOW_H = 56;
constexpr int FOOTER_Y = 280;
constexpr int METRIC_BUFFER_W = PANEL_W - 10;
constexpr int METRIC_BUFFER_H = PANEL_H - 25;
constexpr int FLOW_BUFFER_W = 440;
constexpr int FLOW_BUFFER_H = 32;
constexpr int GRAPH_BUFFER_W = 423;
constexpr int GRAPH_BUFFER_H = 70;
constexpr uint32_t NOTIFICATION_BG = 0x4A2024;
constexpr uint32_t NOTIFICATION_ACCENT = 0xFF5C5C;
constexpr uint32_t NOTIFICATION_TEXT = 0xFFF2F2;

float phHistory[HISTORY_POINTS] = {};
float lightHistory[HISTORY_POINTS] = {};
bool phHistoryValid[HISTORY_POINTS] = {};
bool lightHistoryValid[HISTORY_POINTS] = {};
uint8_t historyHead = 0;
uint8_t historyCount = 0;
uint8_t currentPage = PAGE_NUMERIC;
bool renderBuffersReady = false;
bool headerStateValid = false;
uint8_t drawnHeaderPage = PAGE_NUMERIC;
bool drawnWifiConnected = false;
bool drawnMqttConnected = false;

bool levelFooterStateValid = false;
bool drawnPhUpNormal = false;
bool drawnPhUpValid = false;
bool drawnNutrientANormal = false;
bool drawnNutrientAValid = false;
bool drawnNutrientBNormal = false;
bool drawnNutrientBValid = false;
bool drawnPhDownNormal = false;
bool drawnPhDownValid = false;

bool notificationTargetActive = false;
Display::SensorViewData notificationData{};

void drawNotificationHeader(uint8_t page);

bool phUpIsLow(const Display::SensorViewData &data)
{
    return data.phUpValid && !data.phUpNormal;
}

bool nutrientAIsLow(const Display::SensorViewData &data)
{
    return data.nutrientAValid && data.nutrientANormal;
}

bool nutrientBIsLow(const Display::SensorViewData &data)
{
    return data.nutrientBValid && data.nutrientBNormal;
}

bool phDownIsLow(const Display::SensorViewData &data)
{
    return data.phDownValid && data.phDownNormal;
}

const char *turbidityStatusText(const Display::SensorViewData &data)
{
    if (!data.turbidityValid) return "--";
    if (data.turbidityDirty) return "DIRTY";
    if (data.turbidityCloudy) return "CLOUDY";
    return "CLEAR";
}

uint32_t turbidityStatusColor(const Display::SensorViewData &data)
{
    if (data.turbidityDirty) return CLR_BAD;
    if (data.turbidityCloudy) return CLR_WARN;
    return ACC_TURB;
}

bool hasLowFloat(const Display::SensorViewData &data)
{
    return phUpIsLow(data) || nutrientAIsLow(data) ||
           nutrientBIsLow(data) || phDownIsLow(data);
}

uint8_t lowFloatCount(const Display::SensorViewData &data)
{
    return static_cast<uint8_t>(phUpIsLow(data) + nutrientAIsLow(data) +
                                nutrientBIsLow(data) + phDownIsLow(data));
}

void buildLowFloatList(const Display::SensorViewData &data,
                       char *buffer, size_t bufferSize)
{
    buffer[0] = '\0';
    bool first = true;

    const char *names[] = {"P-UP", "N-A", "N-B", "P-DN"};
    const bool low[] = {phUpIsLow(data), nutrientAIsLow(data),
                        nutrientBIsLow(data), phDownIsLow(data)};

    for (uint8_t i = 0; i < 4; ++i) {
        if (!low[i]) continue;

        const size_t used = strlen(buffer);
        if (used >= bufferSize - 1) break;
        snprintf(buffer + used, bufferSize - used, "%s%s",
                 first ? "" : "  ", names[i]);
        first = false;
    }
}

void initializeRenderBuffers()
{
    renderBuffersReady =
        metricSprite.createSprite(METRIC_BUFFER_W, METRIC_BUFFER_H) != nullptr &&
        flowSprite.createSprite(FLOW_BUFFER_W, FLOW_BUFFER_H) != nullptr &&
        graphSprite.createSprite(GRAPH_BUFFER_W, GRAPH_BUFFER_H) != nullptr;

    if (!renderBuffersReady) {
        metricSprite.deleteSprite();
        flowSprite.deleteSprite();
        graphSprite.deleteSprite();
    }
}

void drawPanelFrame(int x, int y, int w, int h,
                    const char *title, uint32_t accent)
{
    lcd.fillRect(x, y, w, h, CLR_PANEL);
    lcd.drawRect(x, y, w, h, CLR_BORDER);
    lcd.fillRect(x, y, 3, h, accent);

    lcd.setTextDatum(textdatum_t::top_left);
    lcd.setTextColor(CLR_MUTED, CLR_PANEL);
    lcd.setFont(&fonts::Font0);
    lcd.drawString(title, x + 10, y + 7);
}

void drawMetricValue(int x, int y, int w, int h,
                     const char *value, const char *unit,
                     bool valid, uint32_t accent)
{
    if (renderBuffersReady) {
        metricSprite.fillScreen(CLR_PANEL);
        metricSprite.setTextDatum(textdatum_t::middle_center);

        if (!valid) {
            metricSprite.setTextColor(CLR_BAD, CLR_PANEL);
            metricSprite.setFont(&fonts::Font2);
            metricSprite.drawString("NO DATA",
                                    METRIC_BUFFER_W / 2, 25);
        } else {
            metricSprite.setTextColor(accent, CLR_PANEL);
            metricSprite.setFont(&fonts::Font4);
            metricSprite.drawString(value, METRIC_BUFFER_W / 2, 26);
            metricSprite.setTextColor(CLR_MUTED, CLR_PANEL);
            metricSprite.setFont(&fonts::Font0);
            metricSprite.drawString(unit, METRIC_BUFFER_W / 2, 48);
        }

        metricSprite.pushSprite(x + 5, y + 23);
        return;
    }

    lcd.fillRect(x + 5, y + 23, w - 10, h - 25, CLR_PANEL);
    lcd.setTextDatum(textdatum_t::middle_center);

    if (!valid) {
        lcd.setTextColor(CLR_BAD, CLR_PANEL);
        lcd.setFont(&fonts::Font2);
        lcd.drawString("NO DATA", x + w / 2, y + 48);
        return;
    }

    lcd.setTextColor(accent, CLR_PANEL);
    lcd.setFont(&fonts::Font4);
    lcd.drawString(value, x + w / 2, y + 49);

    lcd.setTextColor(CLR_MUTED, CLR_PANEL);
    lcd.setFont(&fonts::Font0);
    lcd.drawString(unit, x + w / 2, y + h - 8);
}

void drawHeader(uint8_t page, bool wifiConnected, bool mqttConnected)
{
    if (notificationTargetActive) {
        drawNotificationHeader(page);
        headerStateValid = true;
        drawnHeaderPage = page;
        drawnWifiConnected = wifiConnected;
        drawnMqttConnected = mqttConnected;
        return;
    }

    lcd.fillRect(0, 0, SCREEN_W, 34, CLR_HEADER);
    lcd.setTextDatum(textdatum_t::top_left);
    lcd.setTextColor(CLR_TEXT, CLR_HEADER);
    lcd.setFont(&fonts::Font2);
    lcd.drawString("HYDRO / TELEMETRY", 8, 6);

    lcd.setFont(&fonts::Font0);
    lcd.setTextColor(wifiConnected ? CLR_OK : CLR_WARN, CLR_HEADER);
    lcd.drawString(wifiConnected ? "WIFI OK" : "WIFI --", 234, 8);
    lcd.setTextColor(mqttConnected ? CLR_OK : CLR_WARN, CLR_HEADER);
    lcd.drawString(mqttConnected ? "MQTT OK" : "MQTT --", 294, 8);

    lcd.setTextDatum(textdatum_t::top_right);
    lcd.setTextColor(CLR_TEXT, CLR_HEADER);
    lcd.drawString(page == PAGE_NUMERIC ? "01 / NUMERIC" : "02 / TRENDS",
                   472, 8);
    lcd.drawFastHLine(8, 33, 464, CLR_BORDER);

    headerStateValid = true;
    drawnHeaderPage = page;
    drawnWifiConnected = wifiConnected;
    drawnMqttConnected = mqttConnected;
}

void drawLevelFooter(const Display::SensorViewData *data)
{
    if (data != nullptr && levelFooterStateValid &&
        drawnPhUpNormal == data->phUpNormal &&
        drawnPhUpValid == data->phUpValid &&
        drawnNutrientANormal == data->nutrientANormal &&
        drawnNutrientAValid == data->nutrientAValid &&
        drawnNutrientBNormal == data->nutrientBNormal &&
        drawnNutrientBValid == data->nutrientBValid &&
        drawnPhDownNormal == data->phDownNormal &&
        drawnPhDownValid == data->phDownValid) {
        return;
    }

    lcd.fillRect(0, FOOTER_Y, SCREEN_W, SCREEN_H - FOOTER_Y, CLR_BG);
    lcd.drawFastHLine(8, FOOTER_Y, 464, CLR_GRID);

    lcd.setTextDatum(textdatum_t::top_left);
    lcd.setFont(&fonts::Font0);
    lcd.setTextColor(CLR_MUTED, CLR_BG);
    lcd.drawString("FLOAT", 8, FOOTER_Y + 8);

    if (data == nullptr) {
        levelFooterStateValid = false;
        lcd.setTextColor(CLR_DIM, CLR_BG);
        lcd.drawString("P-UP:--  N-A:--  N-B:--  P-DN:--",
                       48, FOOTER_Y + 8);
        return;
    }

    const char *pUp = !data->phUpValid ? "--" : (data->phUpNormal ? "OK" : "LOW");
    const char *nutA = !data->nutrientAValid ? "--" : (data->nutrientANormal ? "LOW" : "OK");
    const char *nutB = !data->nutrientBValid ? "--" : (data->nutrientBNormal ? "LOW" : "OK");
    const char *pDown = !data->phDownValid ? "--" : (data->phDownNormal ? "LOW" : "OK");

    char levels[52];
    snprintf(levels, sizeof(levels), "P-UP:%s  N-A:%s  N-B:%s  P-DN:%s",
             pUp, nutA, nutB, pDown);
    lcd.setTextColor(data->phUpNormal ? CLR_OK : CLR_WARN, CLR_BG);
    lcd.drawString(levels, 48, FOOTER_Y + 8);

    levelFooterStateValid = true;
    drawnPhUpNormal = data->phUpNormal;
    drawnPhUpValid = data->phUpValid;
    drawnNutrientANormal = data->nutrientANormal;
    drawnNutrientAValid = data->nutrientAValid;
    drawnNutrientBNormal = data->nutrientBNormal;
    drawnNutrientBValid = data->nutrientBValid;
    drawnPhDownNormal = data->phDownNormal;
    drawnPhDownValid = data->phDownValid;
}

void drawPageIndicator()
{
    lcd.setTextDatum(textdatum_t::top_left);
    lcd.setFont(&fonts::Font0);
    lcd.setTextColor(CLR_DIM, CLR_BG);
    lcd.drawString("AUTO 10s", 8, FOOTER_Y + 25);
    lcd.drawString(currentPage == PAGE_NUMERIC
                       ? "PAGE 1 / 2"
                       : "PAGE 2 / 2",
                   418, FOOTER_Y + 25);
}

void drawNotificationHeader(uint8_t page)
{
    // Notifikasi memakai area header sendiri; card dan grafik tetap utuh.
    lcd.fillRect(0, 0, SCREEN_W, 34, NOTIFICATION_BG);
    lcd.setTextDatum(textdatum_t::top_left);
    lcd.setTextColor(NOTIFICATION_TEXT, NOTIFICATION_BG);
    lcd.setFont(&fonts::Font2);
    lcd.drawString("!! FLOAT LOW", 8, 5);

    char countText[20];
    snprintf(countText, sizeof(countText), "%u SWITCH",
             static_cast<unsigned int>(lowFloatCount(notificationData)));

    char lowList[48];
    buildLowFloatList(notificationData, lowList, sizeof(lowList));
    lcd.setTextDatum(textdatum_t::top_left);
    lcd.setFont(&fonts::Font0);
    lcd.setTextColor(NOTIFICATION_ACCENT, NOTIFICATION_BG);
    lcd.drawString("LOW:", 8, 21);
    lcd.setTextColor(NOTIFICATION_TEXT, NOTIFICATION_BG);
    lcd.drawString(lowList, 40, 21);

    lcd.setTextDatum(textdatum_t::top_right);
    lcd.setFont(&fonts::Font0);
    lcd.drawString(countText, SCREEN_W - 178, 8);
    lcd.setTextColor(NOTIFICATION_TEXT, NOTIFICATION_BG);
    lcd.drawString(page == PAGE_NUMERIC ? "01 / NUMERIC" : "02 / TRENDS",
                   SCREEN_W - 8, 8);
    lcd.drawFastHLine(8, 33, 464, NOTIFICATION_ACCENT);
}

bool updateNotificationTarget(const Display::SensorViewData &data)
{
    const bool active = hasLowFloat(data);
    const bool wasActive = notificationTargetActive;

    if (!active) {
        if (wasActive) {
            notificationTargetActive = false;
            return true;
        }
        return false;
    }

    char oldList[48];
    char newList[48];
    buildLowFloatList(notificationData, oldList, sizeof(oldList));
    buildLowFloatList(data, newList, sizeof(newList));

    notificationData = data;
    notificationTargetActive = true;
    return !wasActive || strcmp(oldList, newList) != 0;
}

void drawNumericLayout()
{
    drawPanelFrame(PANEL_X[0], PANEL_ROW0_Y, PANEL_W, PANEL_H,
                   "TEMP / WATER", ACC_TEMP);
    drawPanelFrame(PANEL_X[1], PANEL_ROW0_Y, PANEL_W, PANEL_H,
                   "PH / ACIDITY", ACC_PH);
    drawPanelFrame(PANEL_X[2], PANEL_ROW0_Y, PANEL_W, PANEL_H,
                   "TDS / SOLIDS", ACC_TDS);
    drawPanelFrame(PANEL_X[0], PANEL_ROW1_Y, PANEL_W, PANEL_H,
                   "LEVEL / DIST", ACC_LEVEL);
    drawPanelFrame(PANEL_X[1], PANEL_ROW1_Y, PANEL_W, PANEL_H,
                   "LIGHT / LUX", ACC_LIGHT);
    drawPanelFrame(PANEL_X[2], PANEL_ROW1_Y, PANEL_W, PANEL_H,
                   "TURB / STATUS", ACC_TURB);

    drawPanelFrame(8, FLOW_Y, 464, FLOW_H, "FLOW / YF-S201", ACC_FLOW);
    lcd.drawFastVLine(239, FLOW_Y + 20, FLOW_H - 20, CLR_GRID);
}

void drawFlowValues(const Display::SensorViewData &data)
{
    char value[20];

    if (renderBuffersReady) {
        flowSprite.fillScreen(CLR_PANEL);
        flowSprite.drawFastVLine(219, 0, FLOW_BUFFER_H, CLR_GRID);
        flowSprite.setTextDatum(textdatum_t::top_left);
        flowSprite.setFont(&fonts::Font0);
        flowSprite.setTextColor(CLR_MUTED, CLR_PANEL);
        flowSprite.drawString("RATE", 0, 3);
        flowSprite.drawString("VOLUME", 232, 3);

        if (!data.flowValid) {
            flowSprite.setTextColor(CLR_BAD, CLR_PANEL);
            flowSprite.setFont(&fonts::Font2);
            flowSprite.drawString("NO DATA", 52, 15);
            flowSprite.drawString("NO DATA", 295, 15);
        } else {
            snprintf(value, sizeof(value), "%.2f", data.flowRateLpm);
            flowSprite.setTextColor(ACC_FLOW, CLR_PANEL);
            flowSprite.setFont(&fonts::Font2);
            flowSprite.drawString(value, 52, 15);
            flowSprite.setTextColor(CLR_MUTED, CLR_PANEL);
            flowSprite.setFont(&fonts::Font0);
            flowSprite.drawString("L/MIN", 106, 20);

            snprintf(value, sizeof(value), "%.3f", data.flowVolumeLiters);
            flowSprite.setTextColor(ACC_FLOW, CLR_PANEL);
            flowSprite.setFont(&fonts::Font2);
            flowSprite.drawString(value, 295, 15);
            flowSprite.setTextColor(CLR_MUTED, CLR_PANEL);
            flowSprite.setFont(&fonts::Font0);
            flowSprite.drawString("L", 385, 20);
        }

        flowSprite.pushSprite(20, FLOW_Y + 20);
        return;
    }

    lcd.setTextDatum(textdatum_t::top_left);
    lcd.setFont(&fonts::Font0);
    lcd.setTextColor(CLR_MUTED, CLR_PANEL);
    lcd.drawString("RATE", 20, FLOW_Y + 23);
    lcd.drawString("VOLUME", 252, FLOW_Y + 23);

    if (!data.flowValid) {
        lcd.setTextColor(CLR_BAD, CLR_PANEL);
        lcd.setFont(&fonts::Font2);
        lcd.drawString("NO DATA", 72, FLOW_Y + 36);
        lcd.drawString("NO DATA", 315, FLOW_Y + 36);
        return;
    }

    snprintf(value, sizeof(value), "%.2f", data.flowRateLpm);
    lcd.setTextColor(ACC_FLOW, CLR_PANEL);
    lcd.setFont(&fonts::Font2);
    lcd.drawString(value, 72, FLOW_Y + 35);
    lcd.setTextColor(CLR_MUTED, CLR_PANEL);
    lcd.setFont(&fonts::Font0);
    lcd.drawString("L/MIN", 126, FLOW_Y + 40);

    snprintf(value, sizeof(value), "%.3f", data.flowVolumeLiters);
    lcd.setTextColor(ACC_FLOW, CLR_PANEL);
    lcd.setFont(&fonts::Font2);
    lcd.drawString(value, 315, FLOW_Y + 35);
    lcd.setTextColor(CLR_MUTED, CLR_PANEL);
    lcd.setFont(&fonts::Font0);
    lcd.drawString("L", 405, FLOW_Y + 40);
}

void updateNumericPage(const Display::SensorViewData &data)
{
    char value[20];

    snprintf(value, sizeof(value), "%.1f", data.temperature);
    drawMetricValue(PANEL_X[0], PANEL_ROW0_Y, PANEL_W, PANEL_H,
                    value, "C", data.tempValid, ACC_TEMP);

    snprintf(value, sizeof(value), "%.2f", data.ph);
    drawMetricValue(PANEL_X[1], PANEL_ROW0_Y, PANEL_W, PANEL_H,
                    value, "pH", data.phValid, ACC_PH);

    snprintf(value, sizeof(value), "%.0f", data.tds);
    drawMetricValue(PANEL_X[2], PANEL_ROW0_Y, PANEL_W, PANEL_H,
                    value, "PPM", data.tdsValid, ACC_TDS);

    snprintf(value, sizeof(value), "%.1f", data.distance);
    drawMetricValue(PANEL_X[0], PANEL_ROW1_Y, PANEL_W, PANEL_H,
                    value, "CM", data.distanceValid, ACC_LEVEL);

    snprintf(value, sizeof(value), "%.0f", data.light);
    drawMetricValue(PANEL_X[1], PANEL_ROW1_Y, PANEL_W, PANEL_H,
                    value, "LUX", data.lightValid, ACC_LIGHT);

    const char *turbidityStatus = turbidityStatusText(data);
    drawMetricValue(PANEL_X[2], PANEL_ROW1_Y, PANEL_W, PANEL_H,
                    turbidityStatus, "STATUS",
                    data.turbidityValid,
                    turbidityStatusColor(data));

    drawFlowValues(data);
    drawLevelFooter(&data);
}

void drawGraphGrid(int x, int y, int w, int h,
                   float minValue, float maxValue,
                   bool present = true)
{
    const int left = x + 34;
    const int right = x + w - 8;
    const int top = y + 27;
    const int bottom = y + h - 16;

    if (renderBuffersReady) {
        graphSprite.fillScreen(CLR_PANEL);
    } else {
        lcd.fillRect(left, top, right - left + 1, bottom - top + 1, CLR_PANEL);
    }

    for (int i = 0; i <= 4; ++i) {
        const int lineY = top + ((bottom - top) * i) / 4;
        if (renderBuffersReady) {
            graphSprite.drawFastHLine(0, lineY - top,
                                      GRAPH_BUFFER_W, CLR_GRID);
        } else {
            lcd.drawFastHLine(left, lineY, right - left + 1, CLR_GRID);
        }

        char label[12];
        snprintf(label, sizeof(label), "%.0f",
                 maxValue - ((maxValue - minValue) * i / 4.0f));
        lcd.setTextDatum(textdatum_t::middle_right);
        lcd.setFont(&fonts::Font0);
        lcd.setTextColor(CLR_MUTED, CLR_PANEL);
        lcd.drawString(label, left - 5, lineY);
    }

    for (int i = 0; i <= 6; ++i) {
        const int lineX = left + ((right - left) * i) / 6;
        if (renderBuffersReady) {
            graphSprite.drawFastVLine(lineX - left, 0,
                                      GRAPH_BUFFER_H, CLR_GRID);
        } else {
            lcd.drawFastVLine(lineX, top, bottom - top + 1, CLR_GRID);
        }
    }

    if (renderBuffersReady && present) {
        graphSprite.pushSprite(left, top);
    }
}

void drawGraphFrame(int x, int y, int w, int h,
                    const char *title, const char *range,
                    uint32_t accent, float minValue, float maxValue)
{
    drawPanelFrame(x, y, w, h, title, accent);
    lcd.setTextDatum(textdatum_t::top_right);
    lcd.setFont(&fonts::Font0);
    lcd.setTextColor(CLR_MUTED, CLR_PANEL);
    lcd.drawString(range, x + w - 8, y + 7);
    drawGraphGrid(x, y, w, h, minValue, maxValue);
}

float lightGraphMax()
{
    float maximum = 1000.0f;
    for (uint8_t i = 0; i < historyCount; ++i) {
        const uint8_t index =
            (historyHead + HISTORY_POINTS - historyCount + i) %
            HISTORY_POINTS;
        if (lightHistoryValid[index] && lightHistory[index] > maximum) {
            maximum = lightHistory[index];
        }
    }

    float step = 500.0f;
    while (maximum > step) step *= 2.0f;
    return step;
}

template <size_t N>
void drawHistorySeries(int x, int y, int w, int h,
                       const float (&history)[N],
                       const bool (&valid)[N],
                       float minValue, float maxValue,
                       uint32_t color)
{
    const int left = x + 34;
    const int right = x + w - 8;
    const int top = y + 27;
    const int bottom = y + h - 16;
    bool havePrevious = false;
    int previousX = 0;
    int previousY = 0;

    for (uint8_t i = 0; i < historyCount; ++i) {
        const uint8_t index =
            (historyHead + HISTORY_POINTS - historyCount + i) %
            HISTORY_POINTS;
        if (!valid[index]) {
            havePrevious = false;
            continue;
        }

        float sample = history[index];
        if (sample < minValue) sample = minValue;
        if (sample > maxValue) sample = maxValue;

        const int pointX = historyCount <= 1
            ? left
            : left + ((right - left) * i) / (historyCount - 1);
        const int pointY = bottom -
            static_cast<int>(((sample - minValue) / (maxValue - minValue)) *
                             (bottom - top));

        if (havePrevious) {
            if (renderBuffersReady) {
                graphSprite.drawLine(previousX - left, previousY - top,
                                     pointX - left, pointY - top, color);
            } else {
                lcd.drawLine(previousX, previousY, pointX, pointY, color);
            }
        }
        previousX = pointX;
        previousY = pointY;
        havePrevious = true;
    }

    if (renderBuffersReady) {
        graphSprite.pushSprite(left, top);
    }
}

void drawTrendsLayout()
{
    drawGraphFrame(8, 40, 464, 112, "PH / HISTORY", "0 - 14 pH",
                   ACC_PH, 0.0f, 14.0f);
    drawGraphFrame(8, 164, 464, 112, "LIGHT / HISTORY", "AUTO LUX",
                   ACC_LIGHT, 0.0f, lightGraphMax());
}

void updateTrendsPage(const Display::SensorViewData &data)
{
    const float maximumLight = lightGraphMax();
    drawGraphGrid(8, 40, 464, 112, 0.0f, 14.0f, false);
    drawHistorySeries(8, 40, 464, 112,
                      phHistory, phHistoryValid, 0.0f, 14.0f, ACC_PH);
    drawGraphGrid(8, 164, 464, 112, 0.0f, maximumLight, false);
    drawHistorySeries(8, 164, 464, 112,
                      lightHistory, lightHistoryValid,
                      0.0f, maximumLight, ACC_LIGHT);

    char temperatureText[12];
    char phText[12];
    char tdsText[12];
    char distanceText[12];
    char lightText[12];
    char flowRateText[12];
    char flowVolumeText[12];

    if (data.tempValid) snprintf(temperatureText, sizeof(temperatureText),
                                 "%.1f", data.temperature);
    else strlcpy(temperatureText, "--", sizeof(temperatureText));
    if (data.phValid) snprintf(phText, sizeof(phText), "%.2f", data.ph);
    else strlcpy(phText, "--", sizeof(phText));
    if (data.tdsValid) snprintf(tdsText, sizeof(tdsText), "%.0f", data.tds);
    else strlcpy(tdsText, "--", sizeof(tdsText));
    if (data.distanceValid) snprintf(distanceText, sizeof(distanceText),
                                     "%.1f", data.distance);
    else strlcpy(distanceText, "--", sizeof(distanceText));
    if (data.lightValid) snprintf(lightText, sizeof(lightText),
                                  "%.0f", data.light);
    else strlcpy(lightText, "--", sizeof(lightText));
    if (data.flowValid) {
        snprintf(flowRateText, sizeof(flowRateText), "%.2f",
                 data.flowRateLpm);
        snprintf(flowVolumeText, sizeof(flowVolumeText), "%.3f",
                 data.flowVolumeLiters);
    } else {
        strlcpy(flowRateText, "--", sizeof(flowRateText));
        strlcpy(flowVolumeText, "--", sizeof(flowVolumeText));
    }

    char lineOne[64];
    char lineTwo[64];
    char lineThree[96];
    snprintf(lineOne, sizeof(lineOne), "T:%sC  PH:%s  TDS:%sppm",
             temperatureText, phText, tdsText);
    const char *turbidityStatus = turbidityStatusText(data);
    snprintf(lineTwo, sizeof(lineTwo), "LV:%scm  Light:%s lux  TB:%s",
             distanceText, lightText, turbidityStatus);

    const char *phUpText = !data.phUpValid ? "--" :
                           (data.phUpNormal ? "OK" : "LOW");
    const char *nutrientAText = !data.nutrientAValid ? "--" :
                                (data.nutrientANormal ? "LOW" : "OK");
    const char *nutrientBText = !data.nutrientBValid ? "--" :
                                (data.nutrientBNormal ? "LOW" : "OK");
    const char *phDownText = !data.phDownValid ? "--" :
                             (data.phDownNormal ? "LOW" : "OK");
    snprintf(lineThree, sizeof(lineThree),
             "F:%sL/m  VOL:%sL  FS P:%s A:%s B:%s D:%s",
             flowRateText, flowVolumeText, phUpText, nutrientAText,
             nutrientBText, phDownText);

    lcd.fillRect(0, FOOTER_Y, SCREEN_W, SCREEN_H - FOOTER_Y, CLR_BG);
    lcd.drawFastHLine(8, FOOTER_Y, 464, CLR_GRID);
    lcd.setTextDatum(textdatum_t::top_left);
    lcd.setFont(&fonts::Font0);
    lcd.setTextColor(CLR_MUTED, CLR_BG);
    lcd.drawString("LAST", 8, FOOTER_Y + 2);
    lcd.setTextColor(CLR_TEXT, CLR_BG);
    lcd.drawString(lineOne, 48, FOOTER_Y + 2);
    lcd.drawString(lineTwo, 8, FOOTER_Y + 14);
    lcd.drawString(lineThree, 8, FOOTER_Y + 26);
}

void recordHistory(const Display::SensorViewData &data)
{
    phHistory[historyHead] = data.ph;
    lightHistory[historyHead] = data.light;
    phHistoryValid[historyHead] = data.phValid;
    lightHistoryValid[historyHead] = data.lightValid;

    historyHead = (historyHead + 1) % HISTORY_POINTS;
    if (historyCount < HISTORY_POINTS) ++historyCount;
}

void drawPageFrame(uint8_t page, const Display::SensorViewData *data)
{
    const bool wifi = data != nullptr && data->wifiConnected;
    const bool mqtt = data != nullptr && data->mqttConnected;
    lcd.fillScreen(CLR_BG);
    drawHeader(page, wifi, mqtt);

    if (page == PAGE_NUMERIC) {
        drawNumericLayout();
        levelFooterStateValid = false;
        drawLevelFooter(data);
        drawPageIndicator();
    } else {
        drawTrendsLayout();
        drawPageIndicator();
    }

}

bool controllerHealthy()
{
    // ILI9488 RDDPM (0x0A): D4=sleep out, D3=normal mode, D2=display on.
    constexpr uint8_t READ_DISPLAY_POWER_MODE = 0x0A;
    constexpr uint8_t REQUIRED_POWER_MODE_BITS = 0x1C;

    const uint8_t status = static_cast<uint8_t>(
        lcd.getPanel()->readCommand(READ_DISPLAY_POWER_MODE, 0, 1) & 0xFFu);

    // Nilai ini biasanya berarti controller tidak merespons atau MISO floating.
    return status != 0x00 && status != 0xFF &&
           (status & REQUIRED_POWER_MODE_BITS) == REQUIRED_POWER_MODE_BITS;
}

bool initializePanel()
{
    // Beri waktu rail 5 V dan controller TFT stabil setelah ESP32 boot.
    delay(150);

    constexpr uint8_t MAX_INIT_ATTEMPTS = 3;
    bool panelStarted = false;
    for (uint8_t attempt = 0; attempt < MAX_INIT_ATTEMPTS; ++attempt) {
        if (lcd.init()) {
            panelStarted = true;
            lcd.setRotation(1); // Landscape 480x320
            delay(25);

            // Jika readback gagal sesaat, ulangi init pada percobaan berikutnya.
            if (controllerHealthy()) return true;
        }

        delay(150);
    }

    // Driver sudah berhasil init; layout tetap digambar agar TFT tidak blank
    // hanya karena MISO belum memberi readback yang stabil saat boot.
    return panelStarted;
}

bool initialized = false;
uint32_t lastHealthCheckMs = 0;
uint32_t lastPageSwitchMs = 0;

} // namespace

namespace Display {

bool begin()
{
    if (!initializePanel()) return false;

    initializeRenderBuffers();
    currentPage = PAGE_NUMERIC;
    drawPageFrame(currentPage, nullptr);

    initialized = true;
    lastHealthCheckMs = millis();
    lastPageSwitchMs = millis();
    return true;
}

void update(const SensorViewData &data)
{
    if (!initialized) return;

    recordHistory(data);
    const bool notificationCleared = updateNotificationTarget(data);

    const uint32_t now = millis();
    bool redrawFrame = notificationCleared;
    if (now - lastHealthCheckMs >= Config::Display::HEALTH_CHECK_INTERVAL_MS) {
        lastHealthCheckMs = now;

        if (!controllerHealthy()) {
            // Pin RST TFT dipakai oleh lcd.init(); ESP32 tidak ikut di-reset.
            if (lcd.init()) {
                lcd.setRotation(1);
                redrawFrame = true;
            }
        }
    }

    if (now - lastPageSwitchMs >= Config::Display::PAGE_ROTATION_INTERVAL_MS) {
        currentPage = currentPage == PAGE_NUMERIC ? PAGE_TRENDS : PAGE_NUMERIC;
        lastPageSwitchMs = now;
        redrawFrame = true;
    }

    if (redrawFrame) {
        drawPageFrame(currentPage, &data);
    } else if (!headerStateValid ||
               drawnHeaderPage != currentPage ||
               drawnWifiConnected != data.wifiConnected ||
               drawnMqttConnected != data.mqttConnected) {
        drawHeader(currentPage, data.wifiConnected, data.mqttConnected);
    }

    if (currentPage == PAGE_NUMERIC) {
        updateNumericPage(data);
    } else {
        updateTrendsPage(data);
    }

}

} // namespace Display
