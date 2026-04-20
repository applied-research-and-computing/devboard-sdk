/**
 * @file scpi_adc.c
 * @brief ADC read SCPI commands
 */

#include <string.h>
#include <stdio.h>
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "scpi_adc";

// ADC handle and calibration
static adc_oneshot_unit_handle_t adc1_handle = NULL;
static adc_cali_handle_t adc1_cali_handle = NULL;
static bool adc_initialized = false;

void scpi_adc_init(void) {
    if (!adc_initialized) {
        // Configure ADC unit
        adc_oneshot_unit_init_cfg_t init_config = {
            .unit_id = ADC_UNIT_1,
        };
        esp_err_t err = adc_oneshot_new_unit(&init_config, &adc1_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ADC unit initialization failed: %s", esp_err_to_name(err));
            return;
        }
        
        // Configure calibration
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = ADC_UNIT_1,
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        err = adc_cali_create_scheme_line_fitting(&cali_config, &adc1_cali_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ADC calibration initialization failed: %s", esp_err_to_name(err));
            adc_oneshot_del_unit(adc1_handle);
            adc1_handle = NULL;
            return;
        }
        
        adc_initialized = true;
        ESP_LOGI(TAG, "ADC initialized (12-bit, 0-3.3V range)");
    }
}

int scpi_handle_adc(const char *cmd, char *response, size_t max_len) {
    if (!adc_initialized || adc1_handle == NULL || adc1_cali_handle == NULL) {
        snprintf(response, max_len, "ERROR: ADC not initialized");
        return strlen(response);
    }

    // ADC:READ? <channel>
    if (strncmp(cmd, "ADC:READ? ", 10) == 0) {
        int channel;
        if (sscanf(cmd + 10, "%d", &channel) != 1) {
            snprintf(response, max_len, "ERROR: Invalid ADC:READ? syntax");
            return strlen(response);
        }
        
        // Validate channel (ADC1 channels 0-7 on ESP32)
        if (channel < 0 || channel > 7) {
            snprintf(response, max_len, "ERROR: Channel must be 0-7");
            return strlen(response);
        }
        
        // Map channel number to ADC1 channel enum
        adc_channel_t adc_channel;
        switch (channel) {
            case 0: adc_channel = ADC_CHANNEL_0; break;  // GPIO36
            case 1: adc_channel = ADC_CHANNEL_1; break;  // GPIO37
            case 2: adc_channel = ADC_CHANNEL_2; break;  // GPIO38
            case 3: adc_channel = ADC_CHANNEL_3; break;  // GPIO39
            case 4: adc_channel = ADC_CHANNEL_4; break;  // GPIO32
            case 5: adc_channel = ADC_CHANNEL_5; break;  // GPIO33
            case 6: adc_channel = ADC_CHANNEL_6; break;  // GPIO34
            case 7: adc_channel = ADC_CHANNEL_7; break;  // GPIO35
            default:
                snprintf(response, max_len, "ERROR: Invalid channel");
                return strlen(response);
        }
        
        // Configure channel
        adc_oneshot_chan_cfg_t config = {
            .bitwidth = ADC_BITWIDTH_DEFAULT,
            .atten = ADC_ATTEN_DB_12,
        };
        esp_err_t err = adc_oneshot_config_channel(adc1_handle, adc_channel, &config);
        if (err != ESP_OK) {
            snprintf(response, max_len, "ERROR: ADC channel config failed");
            return strlen(response);
        }
        
        // Read raw ADC value
        int raw = 0;
        err = adc_oneshot_read(adc1_handle, adc_channel, &raw);
        if (err != ESP_OK) {
            snprintf(response, max_len, "ERROR: ADC read failed");
            return strlen(response);
        }
        
        // Convert to voltage (millivolts)
        int voltage_mv = 0;
        err = adc_cali_raw_to_voltage(adc1_cali_handle, raw, &voltage_mv);
        if (err != ESP_OK) {
            snprintf(response, max_len, "ERROR: ADC calibration failed");
            return strlen(response);
        }
        
        // Convert to volts with 3 decimal places
        float voltage_v = voltage_mv / 1000.0f;
        
        snprintf(response, max_len, "%.3f", voltage_v);
        ESP_LOGI(TAG, "ADC channel %d: %d mV (%.3f V)", channel, voltage_mv, voltage_v);
        return strlen(response);
    }
    
    else {
        snprintf(response, max_len, "ERROR: Unknown ADC command");
        return strlen(response);
    }
}
