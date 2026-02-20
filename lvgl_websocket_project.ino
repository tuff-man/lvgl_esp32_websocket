#include <lvgl.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

// --- PIN Definitions (CYD Board) ---
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33
#define ADC_PIN      34

// --- Display Resolutions ---
#define TFT_HOR_RES 320
#define TFT_VER_RES 240

// --- Global Objects ---
SPIClass touchscreenSpi = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);
uint16_t touchScreenMinimumX = 200, touchScreenMaximumX = 3700;
uint16_t touchScreenMinimumY = 240, touchScreenMaximumY = 3800;

#define DRAW_BUF_SIZE (TFT_HOR_RES * TFT_VER_RES / 10 * (LV_COLOR_DEPTH / 8))
uint8_t* draw_buf;
uint32_t lastTick = 0;

// --- Server/WS ---
AsyncWebServer wsServer(8000);
AsyncWebSocket ws("/ws");

// --- LVGL UI Objects ---
lv_obj_t* tabview;
lv_obj_t* tab_wifi;
lv_obj_t* tab_monitor;

// WiFi UI
lv_obj_t* ap_list;
lv_obj_t* status_label;
lv_obj_t* ip_label;
lv_obj_t* connect_btn;
lv_obj_t* scan_btn;

// Password Modal
lv_obj_t* pwd_msgbox = NULL;
lv_obj_t* pwd_ta = NULL;
lv_obj_t* kb = NULL;

// Monitor UI
lv_obj_t* scale;
lv_obj_t* needle;
lv_obj_t* value_label;
lv_obj_t* log_area;

char selected_ssid[64] = "";

// --- Display/Touch Callbacks ---
void my_disp_flush(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    lv_disp_flush_ready(disp);
}

void my_touchpad_read(lv_indev_t* indev, lv_indev_data_t* data) {
    if (touchscreen.touched()) {
        TS_Point p = touchscreen.getPoint();
        if (p.x < touchScreenMinimumX) touchScreenMinimumX = p.x;
        if (p.x > touchScreenMaximumX) touchScreenMaximumX = p.x;
        if (p.y < touchScreenMinimumY) touchScreenMinimumY = p.y;
        if (p.y > touchScreenMaximumY) touchScreenMaximumY = p.y;
        data->point.x = map(p.x, touchScreenMinimumX, touchScreenMaximumX, 1, TFT_HOR_RES);
        data->point.y = map(p.y, touchScreenMinimumY, touchScreenMaximumY, 1, TFT_VER_RES);
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// --- WiFi Actions ---
void connect_wifi(const char* password) {
    lv_label_set_text_fmt(status_label, "Connecting to %s...", selected_ssid);
    WiFi.begin(selected_ssid, password);
}

void disconnect_wifi() {
    WiFi.disconnect();
    lv_label_set_text(status_label, "Status: Disconnected");
    lv_label_set_text(ip_label, "IP: 0.0.0.0");
}

static void pwd_btn_event_cb(lv_event_t * e) {
    lv_obj_t * btn = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t * mbox = (lv_obj_t *)lv_event_get_user_data(e);
    lv_obj_t * label = lv_obj_get_child(btn, 0);
    const char * txt = lv_label_get_text(label);
    
    if(strcmp(txt, "Connect") == 0) {
        const char * pwd = lv_textarea_get_text(pwd_ta);
        connect_wifi(pwd);
    }
    
    lv_msgbox_close(mbox);
    if(kb) {
        lv_obj_delete(kb);
        kb = NULL;
    }
}

void show_password_modal(const char* ssid) {
    strncpy(selected_ssid, ssid, sizeof(selected_ssid));
    
    pwd_msgbox = lv_msgbox_create(NULL);
    lv_msgbox_add_title(pwd_msgbox, "WiFi Password");
    lv_msgbox_add_text(pwd_msgbox, ssid);
    
    lv_obj_t * btn_connect = lv_msgbox_add_footer_button(pwd_msgbox, "Connect");
    lv_obj_add_event_cb(btn_connect, pwd_btn_event_cb, LV_EVENT_CLICKED, pwd_msgbox);
    
    lv_obj_t * btn_cancel = lv_msgbox_add_footer_button(pwd_msgbox, "Cancel");
    lv_obj_add_event_cb(btn_cancel, pwd_btn_event_cb, LV_EVENT_CLICKED, pwd_msgbox);

    // Move msgbox to the VERY top to avoid keyboard at the bottom
    lv_obj_align(pwd_msgbox, LV_ALIGN_TOP_MID, 0, 0);

    pwd_ta = lv_textarea_create(pwd_msgbox);
    lv_textarea_set_password_mode(pwd_ta, true);
    lv_textarea_set_one_line(pwd_ta, true);
    lv_obj_set_width(pwd_ta, 180); 
    
    // Create keyboard on SIT-WIDE TOP LAYER to ensure it's in front of everything
    kb = lv_keyboard_create(lv_layer_sys()); // Using sys layer for guaranteed front
    lv_keyboard_set_textarea(kb, pwd_ta);
}

void scan_wifi() {
    lv_obj_clean(ap_list);
    lv_label_set_text(status_label, "Scanning...");
    int n = WiFi.scanNetworks();
    if (n == 0) {
        lv_label_set_text(status_label, "No networks found");
    } else {
        lv_label_set_text_fmt(status_label, "%d networks found", n);
        for (int i = 0; i < n; ++i) {
            lv_obj_t* btn = lv_list_add_button(ap_list, LV_SYMBOL_WIFI, WiFi.SSID(i).c_str());
            lv_obj_add_event_cb(btn, [](lv_event_t* e) {
                lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
                const char* ssid = lv_list_get_button_text(ap_list, target);
                show_password_modal(ssid);
            }, LV_EVENT_CLICKED, NULL);
        }
    }
}

// --- WebSocket Event ---
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.printf("WS Client connected: %u\n", client->id());
    } else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("WS Client disconnected: %u\n", client->id());
    } else if (type == WS_EVT_DATA) {
        data[len] = '\0';
        char msg[128];
        snprintf(msg, sizeof(msg), "Client: %s\n", (char*)data);
        lv_textarea_add_text(log_area, msg);
    }
}

// --- UI Creation ---
void create_ui() {
    Serial.println("Creating UI...");
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    
    tabview = lv_tabview_create(scr);
    lv_tabview_set_tab_bar_position(tabview, LV_DIR_TOP);
    lv_obj_set_size(tabview, TFT_HOR_RES, TFT_VER_RES);
    
    // Make tab buttons smaller
    lv_obj_t* tab_bar = lv_tabview_get_tab_bar(tabview);
    lv_obj_set_height(tab_bar, 35);
    lv_obj_set_style_text_font(tab_bar, &lv_font_montserrat_14, 0);

    tab_wifi = lv_tabview_add_tab(tabview, "WiFi");
    tab_monitor = lv_tabview_add_tab(tabview, "Monitor");
    
    Serial.println("Setting up WiFi Tab...");
    // --- WiFi Tab ---
    scan_btn = lv_button_create(tab_wifi);
    lv_obj_set_size(scan_btn, 130, 32);
    lv_obj_align(scan_btn, LV_ALIGN_TOP_LEFT, 5, 5);
    lv_obj_t* scan_label = lv_label_create(scan_btn);
    lv_label_set_text(scan_label, "Scan Wifi");
    lv_obj_set_style_text_font(scan_label, &lv_font_montserrat_14, 0);
    lv_obj_center(scan_label);
    lv_obj_add_event_cb(scan_btn, [](lv_event_t* e) { scan_wifi(); }, LV_EVENT_CLICKED, NULL);
    
    connect_btn = lv_button_create(tab_wifi);
    lv_obj_set_size(connect_btn, 130, 32);
    lv_obj_align(connect_btn, LV_ALIGN_TOP_RIGHT, -5, 5);
    lv_obj_t* conn_label = lv_label_create(connect_btn);
    lv_label_set_text(conn_label, "Disconnect");
    lv_obj_set_style_text_font(conn_label, &lv_font_montserrat_14, 0);
    lv_obj_center(conn_label);
    lv_obj_add_event_cb(connect_btn, [](lv_event_t* e) { disconnect_wifi(); }, LV_EVENT_CLICKED, NULL);

    ap_list = lv_list_create(tab_wifi);
    lv_obj_set_size(ap_list, 300, 70);
    lv_obj_align(ap_list, LV_ALIGN_TOP_MID, 0, 42);
    
    status_label = lv_label_create(tab_wifi);
    lv_obj_align(status_label, LV_ALIGN_TOP_LEFT, 5, 115);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(status_label, "Status: Ready");
    
    ip_label = lv_label_create(tab_wifi);
    lv_obj_align(ip_label, LV_ALIGN_TOP_LEFT, 5, 135);
    lv_obj_set_style_text_font(ip_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(ip_label, "IP: 0.0.0.0");
    
    Serial.println("Setting up Monitor Tab...");
    // --- Monitor Tab ---
    scale = lv_arc_create(tab_monitor);
    lv_obj_set_size(scale, 120, 120);
    lv_obj_align(scale, LV_ALIGN_TOP_MID, 0, 5);
    lv_arc_set_range(scale, 0, 255);
    lv_arc_set_bg_angles(scale, 135, 45); 
    lv_arc_set_value(scale, 0);
    lv_obj_remove_flag(scale, LV_OBJ_FLAG_CLICKABLE); 
    
    lv_obj_set_style_arc_width(scale, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_width(scale, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(scale, lv_palette_main(LV_PALETTE_BLUE), LV_PART_INDICATOR);
    
    value_label = lv_label_create(tab_monitor);
    lv_obj_align(value_label, LV_ALIGN_CENTER, 0, 20); // Relative to tab_monitor
    // Align to scale instead
    lv_obj_align_to(value_label, scale, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_text_font(value_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(value_label, "0");
    
    log_area = lv_textarea_create(tab_monitor);
    lv_obj_set_size(log_area, 300, 52); // 35 * 1.5 = 52.5
    lv_obj_align(log_area, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_set_style_text_font(log_area, &lv_font_montserrat_14, 0); 
    lv_textarea_set_placeholder_text(log_area, "Logs...");
    
    Serial.println("UI Created.");
}

void setup() {
    Serial.begin(115200);
    delay(100); // Give serial some time
    Serial.println("\n--- Starting ESP32 Project ---");
    
    // CRITICAL: Initialize WiFi mode to start the TCP/IP stack (LwIP)
    // This prevents "assert failed: tcpip_api_call ... (Invalid mbox)"
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(); // Clear any persistent connection
    
    analogReadResolution(12);
    
    touchscreenSpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    touchscreen.begin(touchscreenSpi);
    touchscreen.setRotation(3);
    
    lv_init();
    draw_buf = new uint8_t[DRAW_BUF_SIZE];
    lv_display_t* disp = lv_tft_espi_create(TFT_HOR_RES, TFT_VER_RES, draw_buf, DRAW_BUF_SIZE);
    
    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, my_touchpad_read);
    
    create_ui();
    
    ws.onEvent(onWsEvent);
    wsServer.addHandler(&ws);
    wsServer.begin();
    
    Serial.println("Setup complete");
}

void loop() {
    // WiFi Status Update
    static bool wasConnected = false;
    if (WiFi.status() == WL_CONNECTED) {
        if (!wasConnected) {
            lv_label_set_text(status_label, "Connected");
            lv_label_set_text_fmt(ip_label, "IP: %s", WiFi.localIP().toString().c_str());
            wasConnected = true;
        }
    } else {
        if (wasConnected) {
            lv_label_set_text(status_label, "Disconnected");
            lv_label_set_text(ip_label, "IP: 0.0.0.0");
            wasConnected = false;
        }
    }

    // ADC Reading
    int32_t adc_raw = analogRead(ADC_PIN);
    int32_t val_255 = map(adc_raw, 0, 4095, 0, 255);
    
    // Update UI
    static int32_t last_val = -1;
    if (val_255 != last_val) {
        lv_arc_set_value(scale, val_255);
        lv_label_set_text_fmt(value_label, "%d", val_255);
        last_val = val_255;
    }
    
    // Broadcast via WebSocket
    static uint32_t lastBroadcast = 0;
    if (millis() - lastBroadcast > 300) {
        ws.textAll(String(val_255));
        lastBroadcast = millis();
    }
    
    // LVGL Tick
    lv_tick_inc(millis() - lastTick);
    lastTick = millis();
    lv_timer_handler();
    delay(10);
}
