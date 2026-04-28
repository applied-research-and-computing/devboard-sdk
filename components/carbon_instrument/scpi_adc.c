#include "carbon_instrument.h"
#include "carbon_response.h"
#include <string.h>
#include <stdio.h>
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "scpi_adc";

static adc_oneshot_unit_handle_t adc1_handle = NULL;
static adc_cali_handle_t adc1_cali_handle = NULL;
static bool adc_initialized = false;

static int adc_read_handler(const char *cmd, char *r, size_t n)
{
    if (!adc_initialized || adc1_handle == NULL || adc1_cali_handle == NULL) {
        return carbon_respond_error(r, n, 1, "ADC not initialized");
    }

    int channel;
    if (sscanf(cmd + 10, "%d", &channel) != 1) {
        return carbon_respond_error(r, n, 2, "invalid ADC:READ? syntax");
    }
    if (channel < 0 || channel > 7) {
        return carbon_respond_error(r, n, 2, "channel must be 0-7");
    }

    adc_channel_t adc_channel;
    switch (channel) {
        case 0: adc_channel = ADC_CHANNEL_0; break;
        case 1: adc_channel = ADC_CHANNEL_1; break;
        case 2: adc_channel = ADC_CHANNEL_2; break;
        case 3: adc_channel = ADC_CHANNEL_3; break;
        case 4: adc_channel = ADC_CHANNEL_4; break;
        case 5: adc_channel = ADC_CHANNEL_5; break;
        case 6: adc_channel = ADC_CHANNEL_6; break;
        case 7: adc_channel = ADC_CHANNEL_7; break;
        default: return carbon_respond_error(r, n, 2, "invalid channel");
    }

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten    = ADC_ATTEN_DB_12,
    };
    if (adc_oneshot_config_channel(adc1_handle, adc_channel, &config) != ESP_OK) {
        return carbon_respond_error(r, n, 3, "ADC channel config failed");
    }

    int raw = 0;
    if (adc_oneshot_read(adc1_handle, adc_channel, &raw) != ESP_OK) {
        return carbon_respond_error(r, n, 3, "ADC read failed");
    }

    int voltage_mv = 0;
    if (adc_cali_raw_to_voltage(adc1_cali_handle, raw, &voltage_mv) != ESP_OK) {
        return carbon_respond_error(r, n, 3, "ADC calibration failed");
    }

    double voltage_v = voltage_mv / 1000.0;
    ESP_LOGI(TAG, "ADC ch%d: %d mV", channel, voltage_mv);
    return carbon_respond_float(r, n, voltage_v);
}

void scpi_adc_init(void)
{
    if (!adc_initialized) {
        adc_oneshot_unit_init_cfg_t init_config = { .unit_id = ADC_UNIT_1 };
        esp_err_t err = adc_oneshot_new_unit(&init_config, &adc1_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ADC unit init failed: %s", esp_err_to_name(err));
            return;
        }

        adc_cali_line_fitting_config_t cali_config = {
            .unit_id  = ADC_UNIT_1,
            .atten    = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        err = adc_cali_create_scheme_line_fitting(&cali_config, &adc1_cali_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ADC calibration init failed: %s", esp_err_to_name(err));
            adc_oneshot_del_unit(adc1_handle);
            adc1_handle = NULL;
            return;
        }

        adc_initialized = true;
        ESP_LOGI(TAG, "ADC initialized (12-bit, 0-3.3V)");
    }

    static const carbon_cmd_descriptor_t adc_read_cmd = {
        .scpi_command = "ADC:READ?",
        .type         = CARBON_CMD_QUERY,
        .group        = "ADC",
        .description  = "Read ADC channel voltage in volts",
        .params       = {
            { .name = "channel", .type = CARBON_PARAM_INT, .min = 0, .max = 7,
              .description = "ADC1 channel (0=GPIO36..7=GPIO35)", .default_value = "0" },
        },
        .param_count  = 1,
        .timeout_ms   = 500,
        .handler      = adc_read_handler,
    };
    carbon_register_command(&adc_read_cmd);
    ESP_LOGI(TAG, "ADC commands registered");
}
