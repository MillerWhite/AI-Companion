/* KeyPet firmware for the Waveshare ESP32-S3-Touch-AMOLED-1.75C.
   The UI (ui/) is the same code as the simulator; this file only wires the board to it. */
#include "bsp/esp-bsp.h"
#include "bsp/touch.h"
#include "driver/spi_master.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>
#include <time.h>
#include "board.h"
#include "pet.h"
#include "face.h"
#include "pip.h"
#include "shell.h"
#include "theme.h"
#include "phone.h"
#include "profile.h"
#include "mic.h"
#include "voice.h"

static const char *TAG = "keypet";

#define BOOT_GPIO GPIO_NUM_0
int tz_min = 180; /* UTC+3 until the iPhone tells the real zone (ble.c, Current Time Service) */
#define SAVE_EVERY_S 300

/* Wall clock for the pet. Until the RTC is wired or the phone sets the time, the system clock starts in 1970:
   then Pip simply continues from his last save, without catching up the time he was off. */
static int64_t clock_base;
static int64_t pet_clock(void)
{
    int64_t t = time(NULL);
    return t > 1735689600 ? t : clock_base + esp_timer_get_time() / 1000000; /* after 2025-01-01 = real time */
}

static void pet_load(void)
{
    nvs_handle_t h;
    uint8_t buf[256] __attribute__((aligned(8))); /* read as pet_t below: Xtensa faults on misaligned loads */
    size_t n = sizeof buf;
    if (nvs_open("keypet", NVS_READONLY, &h) != ESP_OK) return;
    if (nvs_get_blob(h, "pet", buf, &n) == ESP_OK && pip_pet_restore(buf, n)) {
        clock_base = ((const pet_t *)buf)->last;
        ESP_LOGI(TAG, "pet restored (%u bytes)", (unsigned)n);
    }
    nvs_close(h);
}

/* the style picked in the Style app, kept across restarts */
#define DEV_MODE 0 /* temporary, for the making-of video: the developer overlay over Pip from boot (debug page: dev 0/1) */
static int theme_saved = -1;
static void theme_load(void)
{
    nvs_handle_t h;
    int8_t v;
    if (nvs_open("keypet", NVS_READONLY, &h) != ESP_OK) return;
    if (nvs_get_i8(h, "theme", &v) == ESP_OK && v >= 0 && v < TH_COUNT && !themes[v].hidden) theme_select(v), theme_saved = v;
    nvs_close(h);
}

/* what notifications may show (phone.h PHONE_PRIV_*): kept in NVS when it changes, read back at boot */
static void privacy_save(int mode)
{
    nvs_handle_t h;
    if (nvs_open("keypet", NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_i8(h, "privacy", (int8_t)mode) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

static void privacy_load(void)
{
    nvs_handle_t h;
    int8_t v;
    if (nvs_open("keypet", NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_i8(h, "privacy", &v) == ESP_OK) phone_set_privacy(v);
        nvs_close(h);
    }
    phone_on_privacy(privacy_save);
}

/* The owner's settings and profile (the badge, the QR links), kept in NVS: what the panel changes */
static int bright = 80; /* the panel's full brightness, % */
static volatile bool bright_changed; /* the panel task applies it */
int settings_brightness(void) { return bright; }
void settings_bright(int pct) { bright = pct < 10 ? 10 : pct > 100 ? 100 : pct, bright_changed = true; }

void settings_save(void)
{
    nvs_handle_t h;
    if (nvs_open("keypet", NVS_READWRITE, &h) != ESP_OK) return;
    uint8_t feat = 0;
    for (int f = 0; f < PIP_F_COUNT && f < 8; f++) feat |= (uint8_t)(pip_feature_on(f) << f);
    nvs_set_i16(h, "power", (int16_t)pip_power_after());
    nvs_set_u8(h, "feat", feat);
    nvs_set_u8(h, "bright", (uint8_t)bright);
    nvs_set_i8(h, "privacy", (int8_t)phone_privacy());
    nvs_commit(h);
    nvs_close(h);
}

static void profile_save(void)
{
    nvs_handle_t h;
    if (nvs_open("keypet", NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_blob(h, "profile", &profile, sizeof profile) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

static void settings_load(void)
{
    nvs_handle_t h;
    if (nvs_open("keypet", NVS_READONLY, &h) == ESP_OK) {
        int16_t power;
        uint8_t feat, b;
        size_t n = sizeof profile;
        if (nvs_get_i16(h, "power", &power) == ESP_OK) pip_power(power);
        if (nvs_get_u8(h, "feat", &feat) == ESP_OK)
            for (int f = 0; f < PIP_F_COUNT && f < 8; f++) pip_feature(f, feat >> f & 1);
        if (nvs_get_u8(h, "bright", &b) == ESP_OK) bright = b < 10 ? 10 : b > 100 ? 100 : b;
        if (nvs_get_blob(h, "profile", &profile, &n) != ESP_OK || n != sizeof profile) n = 0; /* a size of its own: the defaults stay */
        nvs_close(h);
    }
    profile_on_change(profile_save);
}

static void theme_keep(void)
{
    int v = theme_index();
    if (v == theme_saved) return;
    nvs_handle_t h;
    if (nvs_open("keypet", NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_i8(h, "theme", (int8_t)v) == ESP_OK) nvs_commit(h), theme_saved = v;
    nvs_close(h);
}

static void pet_save(void)
{
    nvs_handle_t h;
    size_t n;
    const void *p = pip_pet_state(&n);
    if (nvs_open("keypet", NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_blob(h, "pet", p, n) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

/* QMI8658 accelerometer. ponytail: register setup from the datasheet, untested until the board arrives */
#define QMI_WHO_AM_I 0x00 /* reads 0x05 */
#define QMI_CTRL1 0x02    /* 0x40: register auto-increment, little endian */
#define QMI_CTRL2 0x03    /* accel: bits 6..4 range (001 = ±4 g), bits 3..0 rate (0110 = ~117 Hz) */
#define QMI_CTRL3 0x04    /* gyro: bits 6..4 range (101 = ±512 deg/s), bits 3..0 rate, as the accel's */
#define QMI_CTRL7 0x08    /* bit 0: accel enable, bit 1: gyro enable */
#define QMI_AX_L 0x35     /* accel x, y, z, then gyro x, y, z (0x3B): 12 bytes, little endian */
#define QMI_LSB_PER_G 8192.0f
#define QMI_LSB_PER_DPS 64.0f /* at ±512 deg/s */
/* Calibration knob: board axes -> device axes (x right, y toward the top of the screen, z out of the glass).
   Check on the real board: hold it upright facing you, the log must show about (0, +1, 0). */
static const struct { int src; float sign; } AXIS[3] = {{1, -1}, {0, 1}, {2, 1}}; /* sensor turned 90 deg on the board */

static i2c_master_dev_handle_t qmi;
static float stream_rows_per_ms, te_wait_ms, stream_mb_s;
static volatile int frames_sent;
static int64_t render_start;
static float render_ms; /* LVGL's own drawing, without waiting for the panel */ /* frames that reached the panel, for the status line */
static float refr_ms; /* LVGL refresh time, see refr_cb */
static volatile int panel_want = 100, panel_lit = 100; /* the screen: 0 off (asleep), 25 dimmed, 100; the panel task applies it */
static float face_ms[3]; /* face draw: whole, this core's band, the worker's band (ms, smoothed) */
static float refr_worst, face_worst; /* the slowest refresh and face draw since the last status line, ms */
static const char *refr_worst_on = ""; /* the screen it was on */
static volatile float face_last_worker_us;

static esp_err_t qmi_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(qmi, buf, 2, 50);
}

/* Bring-up helper: list every chip on the shared bus (touch, codec, PMIC, IMU, RTC) */
static void i2c_scan(i2c_master_bus_handle_t bus)
{
    for (uint8_t a = 0x08; a < 0x78; a++)
        if (i2c_master_probe(bus, a, 20) == ESP_OK) ESP_LOGI(TAG, "i2c device at 0x%02X", a);
}

static bool qmi_init(i2c_master_bus_handle_t bus)
{
    for (uint8_t addr = 0x6B; addr >= 0x6A; addr--) {
        if (i2c_master_probe(bus, addr, 20) != ESP_OK) continue;
        i2c_device_config_t cfg = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = addr, .scl_speed_hz = 400000};
        ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &cfg, &qmi));
        uint8_t reg = QMI_WHO_AM_I, id = 0;
        i2c_master_transmit_receive(qmi, &reg, 1, &id, 1, 50);
        ESP_LOGI(TAG, "QMI8658 at 0x%02X, WHO_AM_I=0x%02X (expect 0x05)", addr, id);
        return qmi_write(QMI_CTRL1, 0x40) == ESP_OK && qmi_write(QMI_CTRL2, 0x16) == ESP_OK &&
               qmi_write(QMI_CTRL3, 0x56) == ESP_OK && qmi_write(QMI_CTRL7, 0x03) == ESP_OK;
    }
    ESP_LOGW(TAG, "QMI8658 not found at 0x6A/0x6B, running without motion");
    return false;
}

static bool qmi_read(float a[3], float g[3]) /* accel in g, gyro in deg/s, both in device axes (the gyro turns with the chip too) */
{
    uint8_t reg = QMI_AX_L, raw[12];
    if (i2c_master_transmit_receive(qmi, &reg, 1, raw, 12, 50) != ESP_OK) return false;
    float v[6];
    for (int i = 0; i < 6; i++) v[i] = (int16_t)(raw[2 * i] | raw[2 * i + 1] << 8) / (i < 3 ? QMI_LSB_PER_G : QMI_LSB_PER_DPS);
    for (int i = 0; i < 3; i++) a[i] = AXIS[i].sign * v[AXIS[i].src], g[i] = AXIS[i].sign * v[3 + AXIS[i].src];
    return true;
}

/* ---------- AXP2101 power manager (0x34): battery gauge and the PWR key ---------- */
#define AXP_STATUS1 0x00      /* bit 5 VBUS good, bit 3 battery present */
#define AXP_STATUS2 0x01      /* bits 6..5: 01 charging */
#define AXP_GAUGE_CTRL 0x18   /* bit 3: fuel gauge on */
#define AXP_ADC_CTRL 0x30     /* bit 0: battery voltage ADC on */
#define AXP_IRQ_EN2 0x41      /* bit 3: PWR key short press */
#define AXP_IRQ_STATUS2 0x49  /* same bit, write 1 to clear */
#define AXP_BAT_PERCENT 0xA4
static i2c_master_dev_handle_t pmu;

static int pmu_get(uint8_t reg)
{
    uint8_t v;
    return i2c_master_transmit_receive(pmu, &reg, 1, &v, 1, 50) == ESP_OK ? v : -1;
}

static void pmu_set(uint8_t reg, uint8_t v)
{
    uint8_t buf[2] = {reg, v};
    i2c_master_transmit(pmu, buf, 2, 50);
}

static bool pmu_init(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t cfg = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x34, .scl_speed_hz = 400000};
    if (i2c_master_bus_add_device(bus, &cfg, &pmu) != ESP_OK) return false;
    int g = pmu_get(AXP_GAUGE_CTRL), adc = pmu_get(AXP_ADC_CTRL), irq = pmu_get(AXP_IRQ_EN2);
    if (g < 0) return false;
    pmu_set(AXP_GAUGE_CTRL, (uint8_t)(g | 0x08));
    pmu_set(AXP_ADC_CTRL, (uint8_t)(adc | 0x01));
    pmu_set(AXP_IRQ_EN2, (uint8_t)(irq | 0x08));
    pmu_set(AXP_IRQ_STATUS2, 0xFF); /* forget presses from before we started */
    return true;
}

static void pmu_battery(void)
{
    int s1 = pmu_get(AXP_STATUS1), s2 = pmu_get(AXP_STATUS2), pct = pmu_get(AXP_BAT_PERCENT);
    if (s1 < 0) return;
    bool present = s1 & 0x08, charging = s2 >= 0 && ((s2 >> 5) & 0x03) == 0x01;
    shell_battery(present && pct >= 0 && pct <= 100 ? pct : -1, charging);
}

static bool pmu_pwr_pressed(void)
{
    int st = pmu_get(AXP_IRQ_STATUS2);
    if (st < 0 || !(st & 0x08)) return false;
    pmu_set(AXP_IRQ_STATUS2, 0x08);
    return true;
}

/* 50 Hz: accelerometer and the BOOT key into the UI, plus a status line every 5 s */
static void sensors_task(void *arg)
{
    bool imu = qmi_init(bsp_i2c_get_handle());
    bool power = pmu_init(bsp_i2c_get_handle());
    if (!power) ESP_LOGW(TAG, "AXP2101 not answering: no battery level, no PWR key");
    int64_t last_bat = 0;
    gpio_config_t io = {.pin_bit_mask = 1ULL << BOOT_GPIO, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE};
    gpio_config(&io);
    int64_t last = esp_timer_get_time(), last_log = last, last_save = last;
    int boot_prev = 1;
    float mic_peak = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(20));
        int64_t now = esp_timer_get_time();
        float dt = (now - last) / 1e6f, a[3], g[3];
        last = now;
        int boot = gpio_get_level(BOOT_GPIO);
        bool have_a = imu && qmi_read(a, g), pwr = power && pmu_pwr_pressed();

        ui_lock();
        static bool real_time;
        if (!real_time && time(NULL) > 1735689600) { /* the iPhone just set the real clock */
            real_time = true;
            size_t n;
            const pet_t *pet = pip_pet_state(&n);
            if (pet->last < 1735689600) pip_pet_rebase(time(NULL)); /* he lived on a made-up clock: no jump in age */
        }
        if (have_a) shell_imu(a[0], a[1], a[2], dt), shell_gyro(g[0], g[1], g[2], dt);
        if (mic_db() > mic_peak) mic_peak = mic_db();
        shell_time(pet_clock(), tz_min);
        if (now - last_save > SAVE_EVERY_S * 1000000LL) last_save = now, pet_save();
        /* the bottom key is GPIO 0; the top one reaches us as the AXP2101's short-press interrupt
           (a long press is the PMU's own power off) */
        if (boot_prev && !boot) shell_button(PIP_BTN_BOOT);
        if (!boot_prev && boot) shell_button_up(PIP_BTN_BOOT); /* let go: the remote scrolls while it is held */
        if (pwr) shell_button(PIP_BTN_PWR);
        if (power && now - last_bat > 2000000) last_bat = now, pmu_battery();
        theme_keep();
        pip_info_t in;
        pip_get_info(&in);
        panel_want = pip_display_level();
        ui_unlock();
        boot_prev = boot;

        if (now - last_log > 5000000) {
            last_log = now;
            char note[96]; /* the developer HUD's last line: what only the board knows */
            snprintf(note, sizeof note, "озу %u К · psram %.1f М · лицо %.0f мс", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                     heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1048576.0f, face_ms[0]);
            pip_dev_note(note);
            ESP_LOGI(TAG, "mood=%s reaction=%s steps=%d face=%.1f ms refresh=%.1f ms accel=(%.2f %.2f %.2f) free psram=%u internal=%u",
                     in.mood, in.reaction, in.steps, in.frame_ms, refr_ms, have_a ? a[0] : 0, have_a ? a[1] : 0,
                     have_a ? a[2] : 0, (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            ESP_LOGI(TAG, "face draw %.1f ms on two cores (bands %.1f / %.1f ms), battery %s", face_ms[0], face_ms[1], face_ms[2], shell_battery_text());
            ESP_LOGI(TAG, "screen %s: %d frames/s to the panel, render %.1f ms", shell_where(), frames_sent / 5, render_ms);
            frames_sent = 0;
            ESP_LOGI(TAG, "panel: TE wait %.1f ms (in parallel), stream %.0f rows/ms (%.1f MB/s)", te_wait_ms, stream_rows_per_ms, stream_mb_s);
            /* the averages above hide a single slow frame: the worst of the last 5 s, and what was on */
            const char *wm, *wr;
            ui_lock();
            float wf = pip_frame_worst(5, &wm, &wr);
            ui_unlock();
            ESP_LOGI(TAG, "worst in 5 s: Pip's frame %.0f ms (%s%s%s), refresh %.0f ms (%s), face draw %.0f ms", wf, wm, *wr ? ", " : "", wr,
                     refr_worst, refr_worst_on, face_worst);
            refr_worst = face_worst = 0;
            if (mic_on()) ESP_LOGI(TAG, "mic: %.0f dB, loudest %.0f dB in 5 s%s", mic_db(), mic_peak, mic_blowing() ? ", blowing" : "");
            mic_peak = 0;
            ESP_LOGI(TAG, "pet: %02d h food=%.2f energy=%.2f fun=%.2f love=%.2f walk=%.2f day=%+.2f %s", in.hour, in.food,
                     in.energy, in.fun, in.love, in.walk, in.day_mood, in.asleep ? "asleep" : "awake");
        }
    }
}

/* Pip's voice through the ES8311 codec. ponytail: untested until the board arrives; when he is quiet
   the task just sleeps instead of streaming silence. */
#define AUDIO_RATE 16000
#define MIC_GAIN_DB 24 /* ui/mic.c's MIC_SPL_OFFSET assumes this */

/* the ES7210 microphones on the same I2S as the speaker (so the same rate): 20 ms at a time into the meter */
static void mic_task(void *arg)
{
    esp_codec_dev_handle_t mic = bsp_audio_codec_microphone_init();
    esp_codec_dev_sample_info_t fs = {.sample_rate = AUDIO_RATE, .channel = 1, .bits_per_sample = 16};
    if (!mic || esp_codec_dev_open(mic, &fs) != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "microphone init failed: no noise meter, no blowing");
        vTaskDelete(NULL);
    }
    esp_codec_dev_set_in_gain(mic, MIC_GAIN_DB);
    static int16_t buf[AUDIO_RATE / 50];
    for (;;) {
        if (esp_codec_dev_read(mic, buf, sizeof buf) == ESP_CODEC_DEV_OK) mic_feed(buf, AUDIO_RATE / 50, AUDIO_RATE);
        else vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void audio_task(void *arg)
{
    esp_codec_dev_handle_t spk = bsp_audio_codec_speaker_init();
    if (!spk) {
        ESP_LOGW(TAG, "speaker init failed, Pip stays silent");
        vTaskDelete(NULL);
    }
    esp_codec_dev_sample_info_t fs = {.sample_rate = AUDIO_RATE, .channel = 1, .bits_per_sample = 16};
    esp_codec_dev_open(spk, &fs);
    esp_codec_dev_set_out_vol(spk, 70);
    xTaskCreate(mic_task, "mic", 3072, NULL, 4, NULL); /* after the speaker: it set up the I2S both share */
    static int16_t buf[256];
    for (;;) {
        if (!voice_busy()) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        voice_render(buf, 256);
        esp_codec_dev_write(spk, buf, sizeof buf);
    }
}

/* ---------- the display: our own LVGL port, synced to the panel's scan ----------
   The panel's TE (tearing effect) pulse is wired to GPIO 13 (found by probing: 60 Hz, high in vertical blanking);
   the BSP does not use it. LVGL draws only what changed into a full frame buffer in PSRAM (direct mode). When a
   frame is done the changed rows wait for TE and then stream to the panel without a pause, byte-swapped through two
   internal DMA bounce buffers at 80 MHz QSPI: faster than the scan, so the scan, starting right behind us, never
   overtakes a half-sent eye. LVGL draws byte-swapped, so the rows go straight from the frame. */
#define LCD_TE_GPIO 13
#define LCD_QSPI_HZ (80 * 1000 * 1000)
#define LCD_W BSP_LCD_H_RES
#define LCD_H BSP_LCD_V_RES
#define LCD_CHUNK 8192 /* rows go out in chunks this big through two internal DMA buffers (12 KB: barely faster) */
#define LCD_QUEUE 3     /* SPI transactions in flight (the two chunks and the window commands) */
static esp_lcd_panel_io_handle_t lcd_io;
static esp_lcd_panel_handle_t lcd_panel;
static esp_lcd_touch_handle_t lcd_touch;
static SemaphoreHandle_t te_sem, chunk_free, ui_mutex, stream_req, flushed;
static uint8_t *chunk_buf[2]; /* internal DMA buffers the rows go out through, allocated once */
static lv_area_t box, dirty = {0, LCD_H, LCD_W - 1, -1}; /* the rows to send, the rows changed so far */
static const uint16_t *frame;
static lv_display_t *disp_flushing;
/* stream_rows_per_ms must stay well above the scan's ~28 rows/ms */

void ui_lock(void) { xSemaphoreTakeRecursive(ui_mutex, portMAX_DELAY); }
void ui_unlock(void) { xSemaphoreGiveRecursive(ui_mutex); }

static void brightness(int percent) /* the CO5300's 0x51, as the BSP sends it */
{
    uint8_t v = (uint8_t)(percent * 255 / 100);
    esp_lcd_panel_io_tx_param(lcd_io, 0x02 << 24 | 0x51 << 8, &v, 1);
}

static void IRAM_ATTR te_isr(void *arg)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(te_sem, &woken);
    if (woken) portYIELD_FROM_ISR();
}

static bool IRAM_ATTR chunk_sent(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ev, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(chunk_free, &woken);
    return woken == pdTRUE;
}

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    /* direct mode, drawn byte-swapped as the panel wants it: px is the whole frame. Collect the changed rows and,
       when the frame is complete, hand them to the panel task; LVGL may not draw into the frame again until they
       are out (flush_ready comes from there), but the face can render the next frame meanwhile. */
    if (a->y1 < dirty.y1) dirty.y1 = a->y1;
    if (a->y2 > dirty.y2) dirty.y2 = a->y2;
    if (!lv_display_flush_is_last(d)) { lv_display_flush_ready(d); return; }
    render_ms += ((esp_timer_get_time() - render_start) / 1000.0f - render_ms) * 0.1f;
    box = (lv_area_t){0, dirty.y1 & ~1, LCD_W - 1, dirty.y2 | 1}; /* whole rows: contiguous in the frame */
    if (box.y2 >= LCD_H) box.y2 = LCD_H - 1;
    dirty = (lv_area_t){0, LCD_H, LCD_W - 1, -1};
    frame = (const uint16_t *)px, disp_flushing = d;
    xSemaphoreTake(flushed, 0); /* a "done" left over from a flush nobody waited for */
    xSemaphoreGive(stream_req);
}

/* One address window for the rows, then the pixels in chunks: copied from the PSRAM frame into one internal
   buffer while the other goes out by DMA (RAMWR, then "memory write continue"). No allocation per frame: the SPI
   driver's own bounce buffers came from the little internal RAM left with BLE on and slowed the stream. */
static void rows_out(int y1, int y2, const uint16_t *src)
{
    const int x1 = 6, x2 = LCD_W - 1 + 6; /* the panel's column offset, as the BSP sets it */
    uint8_t ca[4] = {x1 >> 8, x1 & 0xFF, x2 >> 8, x2 & 0xFF}, ra[4] = {y1 >> 8, y1 & 0xFF, y2 >> 8, y2 & 0xFF};
    esp_lcd_panel_io_tx_param(lcd_io, 0x02 << 24 | 0x2A << 8, ca, 4);
    esp_lcd_panel_io_tx_param(lcd_io, 0x02 << 24 | 0x2B << 8, ra, 4);
    size_t total = (size_t)(y2 - y1 + 1) * LCD_W * 2, off = 0;
    for (int n = 0; off < total; n ^= 1) {
        size_t len = total - off < LCD_CHUNK ? total - off : LCD_CHUNK;
        if (xSemaphoreTake(chunk_free, pdMS_TO_TICKS(100)) != pdTRUE) break; /* a lost frame beats a frozen screen */
        memcpy(chunk_buf[n], (const uint8_t *)src + off, len);
        esp_lcd_panel_io_tx_color(lcd_io, 0x32 << 24 | (off ? 0x3C : 0x2C) << 8, chunk_buf[n], len);
        off += len;
    }
    for (int k = 0; k < 2; k++) xSemaphoreTake(chunk_free, pdMS_TO_TICKS(100)); /* all out */
    for (int k = 0; k < 2; k++) xSemaphoreGive(chunk_free);
}

/* sends the changed rows: on the next blanking, in one go (one address window, the SPI driver streams the rest).
   Above LVGL's priority so it starts the moment the pulse comes. (Pausing the face meanwhile was tried: the stream
   got no faster, the face 8 ms slower.) */
static void panel_task(void *arg)
{
    for (;;) {
        bool req = xSemaphoreTake(stream_req, pdMS_TO_TICKS(50)) == pdTRUE;
        if (panel_want != panel_lit || bright_changed) { /* on or off: from here, so nothing else talks to the panel meanwhile */
            bright_changed = false;
            int was = panel_lit;
            panel_lit = panel_want;
            if (!panel_lit) brightness(0), esp_lcd_panel_disp_on_off(lcd_panel, false);
            else {
                if (!was) esp_lcd_panel_disp_on_off(lcd_panel, true);
                brightness(bright * panel_lit / 100);
            }
            ESP_LOGI(TAG, "screen %s", !panel_lit ? "off" : panel_lit < 100 ? "dimmed" : "on");
        }
        if (!req) continue;
        int64_t t0 = esp_timer_get_time();
        xSemaphoreTake(te_sem, 0);                 /* an old pulse does not count */
        xSemaphoreTake(te_sem, pdMS_TO_TICKS(25)); /* go on regardless if TE ever stops */
        int64_t t1 = esp_timer_get_time();
        lv_area_t r = box;
        frames_sent++;
        rows_out(r.y1, r.y2, frame + r.y1 * LCD_W);
        int64_t t2 = esp_timer_get_time();
        lv_display_flush_ready(disp_flushing);
        xSemaphoreGive(flushed);
        te_wait_ms += ((t1 - t0) / 1000.0f - te_wait_ms) * 0.1f;
        if (t2 > t1) stream_rows_per_ms += ((r.y2 - r.y1 + 1) * 1000.0f / (t2 - t1) - stream_rows_per_ms) * 0.1f;
        if (t2 > t1) stream_mb_s += ((float)LCD_W * (r.y2 - r.y1 + 1) * 2 / (t2 - t1) - stream_mb_s) * 0.1f;
    }
}

/* LVGL waits for a flush by spinning unless given this: block instead, and leave core 0 to the face and BLE */
static void flush_wait(lv_display_t *d)
{
    (void)d;
    xSemaphoreTake(flushed, pdMS_TO_TICKS(200)); /* given by the panel task right after flush_ready */
}

#define MENU_DEMO 0 /* 1: a pretend finger scrolls the menu after boot, to time it (bring-up bench) */
static void touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
#if MENU_DEMO
    int64_t ms = esp_timer_get_time() / 1000;
    if (ms > 9000) {
        int ph = ms % 1200; /* drag up 300 ms, lift, drag down 300 ms, lift */
        bool down = ph < 300 || (ph >= 600 && ph < 900);
        int t = ph % 600;
        data->point.x = 233;
        data->point.y = ph < 600 ? 380 - t * 260 / 300 : 120 + t * 260 / 300;
        data->state = down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        return;
    }
#endif
    esp_lcd_touch_point_data_t p;
    uint8_t n = 0;
    esp_lcd_touch_read_data(lcd_touch);
    esp_lcd_touch_get_data(lcd_touch, &p, &n, 1);
    if (n) data->point.x = p.x, data->point.y = p.y;
    data->state = n ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static uint32_t tick_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void lvgl_task(void *arg)
{
    for (;;) {
        ui_lock();
        uint32_t wait = lv_timer_handler();
        ui_unlock();
        vTaskDelay(pdMS_TO_TICKS(wait < 1 ? 1 : wait > 10 ? 10 : wait));
    }
}

static lv_display_t *display_start(void)
{
    /* the panel as bsp_display_new() sets it up, but at 80 MHz instead of 40 */
    const co5300_lcd_init_cmd_t init[] = { /* used during esp_lcd_panel_init only */
        {0xFE, (uint8_t[]){0x20}, 1, 0}, {0x19, (uint8_t[]){0x10}, 1, 0}, {0x1C, (uint8_t[]){0xA0}, 1, 0},
        {0xFE, (uint8_t[]){0x00}, 1, 0}, {0xC4, (uint8_t[]){0x80}, 1, 0}, {0x3A, (uint8_t[]){0x55}, 1, 0},
        {0x35, (uint8_t[]){0x00}, 1, 0}, /* TE on: the pulse on GPIO 13 */
        {0x53, (uint8_t[]){0x20}, 1, 0}, {0x51, (uint8_t[]){0xFF}, 1, 0}, {0x63, (uint8_t[]){0xFF}, 1, 0},
        {0x2A, (uint8_t[]){0x00, 0x06, 0x01, 0xD7}, 4, 0}, {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xD1}, 4, 600},
        {0x11, NULL, 0, 600}, {0x29, NULL, 0, 0},
    };
    const spi_bus_config_t bus = CO5300_PANEL_BUS_QSPI_CONFIG(BSP_LCD_PCLK, BSP_LCD_DATA0, BSP_LCD_DATA1, BSP_LCD_DATA2, BSP_LCD_DATA3,
                                                              LCD_CHUNK);
    ESP_ERROR_CHECK(spi_bus_initialize(BSP_LCD_SPI_NUM, &bus, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_spi_config_t io_cfg = CO5300_PANEL_IO_QSPI_CONFIG(BSP_LCD_CS, chunk_sent, NULL);
    io_cfg.pclk_hz = LCD_QSPI_HZ;
    io_cfg.trans_queue_depth = LCD_QUEUE;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BSP_LCD_SPI_NUM, &io_cfg, &lcd_io));
    co5300_vendor_config_t vendor = {.init_cmds = init, .init_cmds_size = sizeof init / sizeof init[0], .flags = {.use_qspi_interface = 1}};
    const esp_lcd_panel_dev_config_t pcfg = {.reset_gpio_num = BSP_LCD_RST, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
                                             .bits_per_pixel = BSP_LCD_BITS_PER_PIXEL, .vendor_config = &vendor};
    ESP_ERROR_CHECK(esp_lcd_new_panel_co5300(lcd_io, &pcfg, &lcd_panel));
    esp_lcd_panel_set_gap(lcd_panel, 0x06, 0);
    esp_lcd_panel_reset(lcd_panel);
    esp_lcd_panel_init(lcd_panel);
    esp_lcd_panel_disp_on_off(lcd_panel, true);

    te_sem = xSemaphoreCreateBinary();
    chunk_free = xSemaphoreCreateCounting(2, 2);
    for (int k = 0; k < 2; k++) chunk_buf[k] = heap_caps_malloc(LCD_CHUNK, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(chunk_buf[0] && chunk_buf[1]);
    stream_req = xSemaphoreCreateBinary();
    flushed = xSemaphoreCreateBinary();
    ui_mutex = xSemaphoreCreateRecursiveMutex();
    gpio_config_t te = {.pin_bit_mask = 1ULL << LCD_TE_GPIO, .mode = GPIO_MODE_INPUT, .intr_type = GPIO_INTR_POSEDGE};
    gpio_config(&te);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(LCD_TE_GPIO, te_isr, NULL);

    lv_init();
    lv_tick_set_cb(tick_ms);
    lv_display_t *disp = lv_display_create(LCD_W, LCD_H);
    size_t fb_size = LCD_W * LCD_H * 2;
    void *fb = heap_caps_aligned_alloc(64, fb_size, MALLOC_CAP_SPIRAM);
    assert(fb);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED); /* big-endian, as the panel takes it */
    lv_display_set_buffers(disp, fb, NULL, fb_size, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_flush_wait_cb(disp, flush_wait);

    bsp_display_cfg_t tcfg = {.touch_flags = {.swap_xy = 0, .mirror_x = 1, .mirror_y = 1}};
    ESP_ERROR_CHECK(bsp_touch_new(&tcfg, &lcd_touch));
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read);
    xTaskCreatePinnedToCore(panel_task, "panel", 4096, NULL, 7, NULL, 0);
    return disp;
}

/* Pip's eyes on both cores: the LVGL task renders one band of rows, this worker the other */
static TaskHandle_t face_worker;
static SemaphoreHandle_t face_done;
static struct { void (*job)(void *, int, int); void *ctx; int y0, y1; } face_band;

static void face_worker_task(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        int64_t t = esp_timer_get_time();
        face_band.job(face_band.ctx, face_band.y0, face_band.y1);
        face_last_worker_us = (float)(esp_timer_get_time() - t);
        face_ms[2] += (face_last_worker_us / 1000.0f - face_ms[2]) * 0.1f;
        xSemaphoreGive(face_done);
    }
}

static float face_split = 0.5f; /* share of the rows for the worker, steered so both bands take as long */
static void face_two_cores(void (*job)(void *, int, int), void *ctx, int y0, int y1)
{
    int mid = y0 + (int)((y1 - y0) * face_split);
    face_band.job = job, face_band.ctx = ctx, face_band.y0 = y0, face_band.y1 = mid;
    int64_t t0 = esp_timer_get_time();
    xTaskNotifyGive(face_worker);
    job(ctx, mid, y1);
    int64_t t1 = esp_timer_get_time();
    xSemaphoreTake(face_done, portMAX_DELAY);
    int64_t t2 = esp_timer_get_time();
    face_ms[0] += ((t2 - t0) / 1000.0f - face_ms[0]) * 0.1f;
    if ((t2 - t0) / 1000.0f > face_worst) face_worst = (t2 - t0) / 1000.0f;
    face_ms[1] += ((t1 - t0) / 1000.0f - face_ms[1]) * 0.1f;
    /* worker band ~ t2 - t0 when it finishes last; move the split toward the slower side's lighter share */
    float mine = (float)(t1 - t0), theirs = face_last_worker_us;
    if (mine + theirs > 0) face_split += 0.15f * (mine - theirs) / (mine + theirs);
    face_split = face_split < 0.15f ? 0.15f : face_split > 0.85f ? 0.85f : face_split;
}

/* The face renders in the background: this task draws the next frame (with the worker, on both cores) while LVGL
   shows the current one. Below LVGL's priority on core 0, so LVGL's own drawing goes first there. */
static TaskHandle_t face_master;
static struct { face_t *f; uint16_t *buf; int w, h; } face_job;

static void face_master_task(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        face_draw(face_job.f, face_job.buf, face_job.w, face_job.h);
        face_busy = 0;
    }
}

static void face_start_bg(face_t *f, uint16_t *buf, int w, int h)
{
    face_job.f = f, face_job.buf = buf, face_job.w = w, face_job.h = h;
    face_busy = 1;
    xTaskNotifyGive(face_master);
}

/* how long LVGL spends on a refresh (render + flush to the panel), averaged, for the status line */
static int64_t refr_start;
static void refr_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_RENDER_START) { render_start = esp_timer_get_time(); return; }
    if (lv_event_get_code(e) == LV_EVENT_REFR_START) refr_start = esp_timer_get_time();
    else {
        float ms = (esp_timer_get_time() - refr_start) / 1000.0f;
        refr_ms += (ms - refr_ms) * 0.1f;
        if (ms > refr_worst) refr_worst = ms, refr_worst_on = shell_where();
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    face_done = xSemaphoreCreateBinary();
    /* on the core LVGL is not pinned to; same priority so it gets the CPU as soon as a band is ready */
    xTaskCreatePinnedToCore(face_worker_task, "face", 6144, NULL, 6, &face_worker, 1);
    face_parallel = face_two_cores;
    xTaskCreatePinnedToCore(face_master_task, "face bg", 6144, NULL, 4, &face_master, 0);
    face_start = face_start_bg;
    lv_display_t *disp = display_start();
    brightness(bright);
    i2c_scan(bsp_i2c_get_handle());

    ui_lock();
    lv_rand_set_seed(esp_random());
    theme_load(); /* before the UI is built in it */
    privacy_load();
    settings_load();
    lv_display_add_event_cb(disp, refr_cb, LV_EVENT_REFR_START, NULL);
    lv_display_add_event_cb(disp, refr_cb, LV_EVENT_REFR_READY, NULL);
    lv_display_add_event_cb(disp, refr_cb, LV_EVENT_RENDER_START, NULL);
    shell_create();
    pip_wake_up(); /* out of black, sleepy, a shake */
    if (DEV_MODE) pip_dev(true);
    pet_load();
    shell_time(pet_clock(), tz_min);
#if MENU_DEMO
    shell_button(PIP_BTN_PWR); /* into the menu */
#endif
    ui_unlock();
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, NULL, 6, NULL, 0); /* core 0; the face worker has core 1 */

    voice_init(AUDIO_RATE);
    xTaskCreate(sensors_task, "sensors", 4096, NULL, 5, NULL);
    xTaskCreate(audio_task, "audio", 4096, NULL, 6, NULL);
    ble_start();
    debug_start();
}
