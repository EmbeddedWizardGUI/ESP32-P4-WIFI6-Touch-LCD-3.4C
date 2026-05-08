/*******************************************************************************
*
* Embedded Wizard - GUI Solutions by TARA Systems
*
*                                                Copyright (c) TARA Systems GmbH
*                                    written by Paul Banach and Manfred Schweyer
*
********************************************************************************
*
* This software is provided as example code to demonstrate the use of Embedded
* Wizard and related software components. It may be used in production systems
* after you have reviewed, tested and adapted it to your specific requirements.
*
* Use of this example code is subject to the Embedded Wizard license terms
* (as published at https://www.embedded-wizard.de/legal), including in
* particular:
*
*   - Embedded Wizard Terms and Conditions (EWTC)
*   - Embedded Wizard License Agreement (EWLA)
*   - Embedded Wizard Community License (EWCL)
*
* The specific agreement(s) applicable to you depend on your contractual
* relationship with TARA Systems GmbH and/or your use of the Community License.
*
* Subject to your compliance with the applicable Embedded Wizard license terms
* and/or any separate written agreement with TARA Systems, you are granted a
* non-exclusive, worldwide, royalty-free license to use, copy, modify and
* integrate this example code into your own software products and projects.
* You may redistribute this code only as part of your products and not as a
* standalone library, framework or development tool.
*
* THE SOFTWARE IS PROVIDED "AS IS" AND "AS AVAILABLE", WITHOUT WARRANTY OF ANY
* KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT.
* TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, IN NO EVENT SHALL TARA
* SYSTEMS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
* ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
* WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*
********************************************************************************
*
* DESCRIPTION:
*   This file is part of the interface (glue layer) between an Embedded Wizard
*   generated UI application and the board support package (BSP) of a dedicated
*   target.
*   This template is responsible to initialize the display hardware of the board
*   and to provide the necessary access to update the display content.
*   The color format of the framebuffer has to correspond to the color format
*   of the Graphics Engine.
*
*   Important: This file is intended to be used as a template. Please adapt the
*   implementation according your particular hardware.
*
*******************************************************************************/

#include "esp_system.h"
#include "esp_heap_caps.h"

#include <string.h>

#include "ewconfig.h"
#include "ewrte.h"
#include "ewgfx.h"
#include "ewextgfx.h"
#include "ewgfxdefs.h"

#include "ew_bsp_os.h"
#include "ew_bsp_display.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include <stdio.h>
#include <esp_lcd_panel_ops.h>
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_jd9365.h"
#include "esp_ldo_regulator.h"
#include "driver/ledc.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Hardware pin definitions
#define DISPLAY_I2C_NUM          (0)
#define DISPLAY_I2C_SDA_GPIO     (7)
#define DISPLAY_I2C_SCL_GPIO     (8)
#define DISPLAY_I2C_CLK_SPEED_HZ (400000)

#define DISPLAY_LCD_BACKLIGHT    (26)
#define DISPLAY_LCD_RST          (27)

#define DISPLAY_H_RES            (800)
#define DISPLAY_V_RES            (800)

#define DISPLAY_MIPI_DSI_LANE_NUM          (2)
#define DISPLAY_MIPI_DSI_LANE_BITRATE_MBPS (1500)
#define DISPLAY_MIPI_DSI_PHY_PWR_LDO_CHAN  (3)
#define DISPLAY_MIPI_DSI_PHY_PWR_LDO_MV    (2500)

#define DISPLAY_LEDC_CHANNEL     (0)
#define DISPLAY_LEDC_TIMER       (1)

static esp_lcd_panel_handle_t PanelHandle = NULL;

#if ( EW_FRAME_BUFFER_COLOR_FORMAT == EW_FRAME_BUFFER_COLOR_FORMAT_RGB565 )

#else
  #error "selected EW_FRAME_BUFFER_COLOR_FORMAT not supported"
#endif

#if EW_USE_DOUBLE_BUFFER == 0

  #error "Double buffering required for ESP32-P4-Function-EV-Board!"

#endif

#if EW_USE_OPERATING_SYSTEM == 1

  static XSemaphoreHandle     LcdUpdateSemaphore = 0;

#endif


static const char *TAG = "EW_BSP_DISPLAY";

// LCD initialization commands for JD9365 (800x800 3.4")
static const jd9365_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xE0, (uint8_t[]){0x00}, 1, 0}, {0xE1, (uint8_t[]){0x93}, 1, 0},
    {0xE2, (uint8_t[]){0x65}, 1, 0}, {0xE3, (uint8_t[]){0xF8}, 1, 0},
    {0x80, (uint8_t[]){0x01}, 1, 0}, {0xE0, (uint8_t[]){0x01}, 1, 0},
    {0x00, (uint8_t[]){0x00}, 1, 0}, {0x01, (uint8_t[]){0x41}, 1, 0},
    {0x03, (uint8_t[]){0x10}, 1, 0}, {0x04, (uint8_t[]){0x44}, 1, 0},
    {0x17, (uint8_t[]){0x00}, 1, 0}, {0x18, (uint8_t[]){0xD0}, 1, 0},
    {0x19, (uint8_t[]){0x00}, 1, 0}, {0x1A, (uint8_t[]){0x00}, 1, 0},
    {0x1B, (uint8_t[]){0xD0}, 1, 0}, {0x1C, (uint8_t[]){0x00}, 1, 0},
    {0x24, (uint8_t[]){0xFE}, 1, 0}, {0x35, (uint8_t[]){0x26}, 1, 0},
    {0x37, (uint8_t[]){0x09}, 1, 0}, {0x38, (uint8_t[]){0x04}, 1, 0},
    {0x39, (uint8_t[]){0x08}, 1, 0}, {0x3A, (uint8_t[]){0x0A}, 1, 0},
    {0x3C, (uint8_t[]){0x78}, 1, 0}, {0x3D, (uint8_t[]){0xFF}, 1, 0},
    {0x3E, (uint8_t[]){0xFF}, 1, 0}, {0x3F, (uint8_t[]){0xFF}, 1, 0},
    {0x40, (uint8_t[]){0x00}, 1, 0}, {0x41, (uint8_t[]){0x64}, 1, 0},
    {0x42, (uint8_t[]){0xC7}, 1, 0}, {0x43, (uint8_t[]){0x18}, 1, 0},
    {0x44, (uint8_t[]){0x0B}, 1, 0}, {0x45, (uint8_t[]){0x14}, 1, 0},
    {0x55, (uint8_t[]){0x02}, 1, 0}, {0x57, (uint8_t[]){0x49}, 1, 0},
    {0x59, (uint8_t[]){0x0A}, 1, 0}, {0x5A, (uint8_t[]){0x1B}, 1, 0},
    {0x5B, (uint8_t[]){0x19}, 1, 0}, {0x5D, (uint8_t[]){0x7F}, 1, 0},
    {0x5E, (uint8_t[]){0x56}, 1, 0}, {0x5F, (uint8_t[]){0x43}, 1, 0},
    {0x60, (uint8_t[]){0x37}, 1, 0}, {0x61, (uint8_t[]){0x33}, 1, 0},
    {0x62, (uint8_t[]){0x25}, 1, 0}, {0x63, (uint8_t[]){0x2A}, 1, 0},
    {0x64, (uint8_t[]){0x16}, 1, 0}, {0x65, (uint8_t[]){0x30}, 1, 0},
    {0x66, (uint8_t[]){0x2F}, 1, 0}, {0x67, (uint8_t[]){0x32}, 1, 0},
    {0x68, (uint8_t[]){0x53}, 1, 0}, {0x69, (uint8_t[]){0x43}, 1, 0},
    {0x6A, (uint8_t[]){0x4C}, 1, 0}, {0x6B, (uint8_t[]){0x40}, 1, 0},
    {0x6C, (uint8_t[]){0x3D}, 1, 0}, {0x6D, (uint8_t[]){0x31}, 1, 0},
    {0x6E, (uint8_t[]){0x20}, 1, 0}, {0x6F, (uint8_t[]){0x0F}, 1, 0},
    {0x70, (uint8_t[]){0x7F}, 1, 0}, {0x71, (uint8_t[]){0x56}, 1, 0},
    {0x72, (uint8_t[]){0x43}, 1, 0}, {0x73, (uint8_t[]){0x37}, 1, 0},
    {0x74, (uint8_t[]){0x33}, 1, 0}, {0x75, (uint8_t[]){0x25}, 1, 0},
    {0x76, (uint8_t[]){0x2A}, 1, 0}, {0x77, (uint8_t[]){0x16}, 1, 0},
    {0x78, (uint8_t[]){0x30}, 1, 0}, {0x79, (uint8_t[]){0x2F}, 1, 0},
    {0x7A, (uint8_t[]){0x32}, 1, 0}, {0x7B, (uint8_t[]){0x53}, 1, 0},
    {0x7C, (uint8_t[]){0x43}, 1, 0}, {0x7D, (uint8_t[]){0x4C}, 1, 0},
    {0x7E, (uint8_t[]){0x40}, 1, 0}, {0x7F, (uint8_t[]){0x3D}, 1, 0},
    {0x80, (uint8_t[]){0x31}, 1, 0}, {0x81, (uint8_t[]){0x20}, 1, 0},
    {0x82, (uint8_t[]){0x0F}, 1, 0}, {0xE0, (uint8_t[]){0x02}, 1, 0},
    {0x00, (uint8_t[]){0x5F}, 1, 0}, {0x01, (uint8_t[]){0x5F}, 1, 0},
    {0x02, (uint8_t[]){0x5E}, 1, 0}, {0x03, (uint8_t[]){0x5E}, 1, 0},
    {0x04, (uint8_t[]){0x50}, 1, 0}, {0x05, (uint8_t[]){0x48}, 1, 0},
    {0x06, (uint8_t[]){0x48}, 1, 0}, {0x07, (uint8_t[]){0x4A}, 1, 0},
    {0x08, (uint8_t[]){0x4A}, 1, 0}, {0x09, (uint8_t[]){0x44}, 1, 0},
    {0x0A, (uint8_t[]){0x44}, 1, 0}, {0x0B, (uint8_t[]){0x46}, 1, 0},
    {0x0C, (uint8_t[]){0x46}, 1, 0}, {0x0D, (uint8_t[]){0x5F}, 1, 0},
    {0x0E, (uint8_t[]){0x5F}, 1, 0}, {0x0F, (uint8_t[]){0x57}, 1, 0},
    {0x10, (uint8_t[]){0x57}, 1, 0}, {0x11, (uint8_t[]){0x77}, 1, 0},
    {0x12, (uint8_t[]){0x77}, 1, 0}, {0x13, (uint8_t[]){0x40}, 1, 0},
    {0x14, (uint8_t[]){0x42}, 1, 0}, {0x15, (uint8_t[]){0x5F}, 1, 0},
    {0x16, (uint8_t[]){0x5F}, 1, 0}, {0x17, (uint8_t[]){0x5F}, 1, 0},
    {0x18, (uint8_t[]){0x5E}, 1, 0}, {0x19, (uint8_t[]){0x5E}, 1, 0},
    {0x1A, (uint8_t[]){0x50}, 1, 0}, {0x1B, (uint8_t[]){0x49}, 1, 0},
    {0x1C, (uint8_t[]){0x49}, 1, 0}, {0x1D, (uint8_t[]){0x4B}, 1, 0},
    {0x1E, (uint8_t[]){0x4B}, 1, 0}, {0x1F, (uint8_t[]){0x45}, 1, 0},
    {0x20, (uint8_t[]){0x45}, 1, 0}, {0x21, (uint8_t[]){0x47}, 1, 0},
    {0x22, (uint8_t[]){0x47}, 1, 0}, {0x23, (uint8_t[]){0x5F}, 1, 0},
    {0x24, (uint8_t[]){0x5F}, 1, 0}, {0x25, (uint8_t[]){0x57}, 1, 0},
    {0x26, (uint8_t[]){0x57}, 1, 0}, {0x27, (uint8_t[]){0x77}, 1, 0},
    {0x28, (uint8_t[]){0x77}, 1, 0}, {0x29, (uint8_t[]){0x41}, 1, 0},
    {0x2A, (uint8_t[]){0x43}, 1, 0}, {0x2B, (uint8_t[]){0x5F}, 1, 0},
    {0x2C, (uint8_t[]){0x1E}, 1, 0}, {0x2D, (uint8_t[]){0x1E}, 1, 0},
    {0x2E, (uint8_t[]){0x1F}, 1, 0}, {0x2F, (uint8_t[]){0x1F}, 1, 0},
    {0x30, (uint8_t[]){0x10}, 1, 0}, {0x31, (uint8_t[]){0x07}, 1, 0},
    {0x32, (uint8_t[]){0x07}, 1, 0}, {0x33, (uint8_t[]){0x05}, 1, 0},
    {0x34, (uint8_t[]){0x05}, 1, 0}, {0x35, (uint8_t[]){0x0B}, 1, 0},
    {0x36, (uint8_t[]){0x0B}, 1, 0}, {0x37, (uint8_t[]){0x09}, 1, 0},
    {0x38, (uint8_t[]){0x09}, 1, 0}, {0x39, (uint8_t[]){0x1F}, 1, 0},
    {0x3A, (uint8_t[]){0x1F}, 1, 0}, {0x3B, (uint8_t[]){0x17}, 1, 0},
    {0x3C, (uint8_t[]){0x17}, 1, 0}, {0x3D, (uint8_t[]){0x17}, 1, 0},
    {0x3E, (uint8_t[]){0x17}, 1, 0}, {0x3F, (uint8_t[]){0x03}, 1, 0},
    {0x40, (uint8_t[]){0x01}, 1, 0}, {0x41, (uint8_t[]){0x1F}, 1, 0},
    {0x42, (uint8_t[]){0x1E}, 1, 0}, {0x43, (uint8_t[]){0x1E}, 1, 0},
    {0x44, (uint8_t[]){0x1F}, 1, 0}, {0x45, (uint8_t[]){0x1F}, 1, 0},
    {0x46, (uint8_t[]){0x10}, 1, 0}, {0x47, (uint8_t[]){0x06}, 1, 0},
    {0x48, (uint8_t[]){0x06}, 1, 0}, {0x49, (uint8_t[]){0x04}, 1, 0},
    {0x4A, (uint8_t[]){0x04}, 1, 0}, {0x4B, (uint8_t[]){0x0A}, 1, 0},
    {0x4C, (uint8_t[]){0x0A}, 1, 0}, {0x4D, (uint8_t[]){0x08}, 1, 0},
    {0x4E, (uint8_t[]){0x08}, 1, 0}, {0x4F, (uint8_t[]){0x1F}, 1, 0},
    {0x50, (uint8_t[]){0x1F}, 1, 0}, {0x51, (uint8_t[]){0x17}, 1, 0},
    {0x52, (uint8_t[]){0x17}, 1, 0}, {0x53, (uint8_t[]){0x17}, 1, 0},
    {0x54, (uint8_t[]){0x17}, 1, 0}, {0x55, (uint8_t[]){0x02}, 1, 0},
    {0x56, (uint8_t[]){0x00}, 1, 0}, {0x57, (uint8_t[]){0x1F}, 1, 0},
    {0xE0, (uint8_t[]){0x02}, 1, 0}, {0x58, (uint8_t[]){0x40}, 1, 0},
    {0x59, (uint8_t[]){0x00}, 1, 0}, {0x5A, (uint8_t[]){0x00}, 1, 0},
    {0x5B, (uint8_t[]){0x30}, 1, 0}, {0x5C, (uint8_t[]){0x01}, 1, 0},
    {0x5D, (uint8_t[]){0x30}, 1, 0}, {0x5E, (uint8_t[]){0x01}, 1, 0},
    {0x5F, (uint8_t[]){0x02}, 1, 0}, {0x60, (uint8_t[]){0x30}, 1, 0},
    {0x61, (uint8_t[]){0x03}, 1, 0}, {0x62, (uint8_t[]){0x04}, 1, 0},
    {0x63, (uint8_t[]){0x04}, 1, 0}, {0x64, (uint8_t[]){0xA6}, 1, 0},
    {0x65, (uint8_t[]){0x43}, 1, 0}, {0x66, (uint8_t[]){0x30}, 1, 0},
    {0x67, (uint8_t[]){0x73}, 1, 0}, {0x68, (uint8_t[]){0x05}, 1, 0},
    {0x69, (uint8_t[]){0x04}, 1, 0}, {0x6A, (uint8_t[]){0x7F}, 1, 0},
    {0x6B, (uint8_t[]){0x08}, 1, 0}, {0x6C, (uint8_t[]){0x00}, 1, 0},
    {0x6D, (uint8_t[]){0x04}, 1, 0}, {0x6E, (uint8_t[]){0x04}, 1, 0},
    {0x6F, (uint8_t[]){0x88}, 1, 0}, {0x75, (uint8_t[]){0xD9}, 1, 0},
    {0x76, (uint8_t[]){0x00}, 1, 0}, {0x77, (uint8_t[]){0x33}, 1, 0},
    {0x78, (uint8_t[]){0x43}, 1, 0}, {0xE0, (uint8_t[]){0x00}, 1, 0},
    {0x11, (uint8_t[]){0x00}, 1, 120}, {0x29, (uint8_t[]){0x00}, 1, 20},
    {0x35, (uint8_t[]){0x00}, 1, 0},
};

static volatile uint32_t    CurrentFramebuffer = 0;
static volatile uint32_t    PendingFramebuffer = 0;

IRAM_ATTR static bool VSyncCallback( esp_lcd_panel_handle_t panel,
  esp_lcd_dpi_panel_event_data_t* edata, void* user_ctx )
{
  register uint32_t pendingBuffer = PendingFramebuffer;
  if ( CurrentFramebuffer != pendingBuffer )
  {
    #if EW_USE_OPERATING_SYSTEM == 1

      EwBspOsSemaphoreRelease( LcdUpdateSemaphore );

    #else

      /* save new address */
      CurrentFramebuffer = pendingBuffer;

    #endif
  }
  return false;
}


/*******************************************************************************
* FUNCTION:
*   EwBspDisplayBrightnessInit (static helper)
*
* DESCRIPTION:
*   Initialize display backlight PWM control.
*
*******************************************************************************/
static esp_err_t EwBspDisplayBrightnessInit(void)
{
    const ledc_timer_config_t backlight_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = DISPLAY_LEDC_TIMER,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&backlight_timer), TAG, "LEDC timer config failed");

    const ledc_channel_config_t backlight_channel = {
        .gpio_num = DISPLAY_LCD_BACKLIGHT,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = DISPLAY_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = DISPLAY_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
        .flags = { .output_invert = 1 }
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&backlight_channel), TAG, "LEDC channel config failed");

    return ESP_OK;
}


/*******************************************************************************
* FUNCTION:
*   EwBspDisplayBrightnessSet (static helper)
*
* DESCRIPTION:
*   Set display backlight brightness level.
*
*******************************************************************************/
static esp_err_t EwBspDisplayBrightnessSet(int brightness_percent)
{
    if (brightness_percent > 100)
        brightness_percent = 100;
    else if (brightness_percent < 0)
        brightness_percent = 0;

    uint32_t duty_cycle = (1023 * brightness_percent) / 100;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, DISPLAY_LEDC_CHANNEL, duty_cycle), TAG, "");
    ESP_RETURN_ON_ERROR(ledc_update_duty(LEDC_LOW_SPEED_MODE, DISPLAY_LEDC_CHANNEL), TAG, "");

    return ESP_OK;
}


/*******************************************************************************
* FUNCTION:
*   EwBspEnableDsiPhyPower (static helper)
*
* DESCRIPTION:
*   Enable MIPI DSI PHY power.
*
*******************************************************************************/
static esp_err_t EwBspEnableDsiPhyPower(void)
{
    static esp_ldo_channel_handle_t phy_pwr_chan = NULL;
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id = DISPLAY_MIPI_DSI_PHY_PWR_LDO_CHAN,
        .voltage_mv = DISPLAY_MIPI_DSI_PHY_PWR_LDO_MV,
    };
    ESP_RETURN_ON_ERROR(
        esp_ldo_acquire_channel(&ldo_cfg, &phy_pwr_chan),
        TAG,
        "Acquire LDO channel for DPHY failed"
    );

    return ESP_OK;
}


/*******************************************************************************
* FUNCTION:
*   EwBspDisplayLcdInit (static helper)
*
* DESCRIPTION:
*   Initialize JD9365 LCD display via MIPI DSI.
*
*******************************************************************************/
typedef struct {
    esp_lcd_dsi_bus_handle_t    mipi_dsi_bus;
    esp_lcd_panel_io_handle_t   io;
    esp_lcd_panel_handle_t      panel;
} display_handles_t;

static esp_err_t EwBspDisplayLcdInit(display_handles_t *ret_handles)
{
    ESP_RETURN_ON_ERROR(EwBspEnableDsiPhyPower(), TAG, "DSI PHY power failed");

    // Create MIPI DSI bus
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus;
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = DISPLAY_MIPI_DSI_LANE_NUM,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = DISPLAY_MIPI_DSI_LANE_BITRATE_MBPS,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus), TAG, "DSI bus init failed");

    // Create DBI panel IO
    esp_lcd_panel_io_handle_t io;
    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &io), TAG, "Panel IO failed");

    // Configure DPI panel
    esp_lcd_dpi_panel_config_t dpi_config = {
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 44,
        .virtual_channel = 0,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs = 2,  // Use 2 framebuffers for Embedded Wizard double buffering
        .video_timing = {
            .h_size = DISPLAY_H_RES,
            .v_size = DISPLAY_V_RES,
            .hsync_back_porch = 20,
            .hsync_pulse_width = 20,
            .hsync_front_porch = 40,
            .vsync_back_porch = 12,
            .vsync_pulse_width = 4,
            .vsync_front_porch = 24,
        },
        .flags.use_dma2d = true,
    };

    // JD9365 vendor config
    jd9365_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .mipi_config = {
            .dsi_bus = mipi_dsi_bus,
            .dpi_config = &dpi_config,
            .lane_num = DISPLAY_MIPI_DSI_LANE_NUM,
        },
    };

    esp_lcd_panel_dev_config_t lcd_dev_config = {
        .bits_per_pixel = 16,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .reset_gpio_num = DISPLAY_LCD_RST,
        .vendor_config = &vendor_config,
    };

    esp_lcd_panel_handle_t panel = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_jd9365(io, &lcd_dev_config, &panel), TAG, "New panel failed");

    // Reset and initialize the panel
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "Panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "Panel init failed");

    // Small delay after init
    vTaskDelay(pdMS_TO_TICKS(100));

    ret_handles->mipi_dsi_bus = mipi_dsi_bus;
    ret_handles->io = io;
    ret_handles->panel = panel;

    return ESP_OK;
}


/*******************************************************************************
* FUNCTION:
*   EwBspDisplayInit
*
* DESCRIPTION:
*   The function EwBspDisplayInit initializes the display hardware and returns
*   the display parameter.
*
* ARGUMENTS:
*   aGuiWidth,
*   aGuiHeight   - Size of the GUI in pixel.
*   aDisplayInfo - Display info data structure.
*
* RETURN VALUE:
*   Returns 1 if successful, 0 otherwise.
*
*******************************************************************************/
int EwBspDisplayInit( int aGuiWidth, int aGuiHeight, XDisplayInfo* aDisplayInfo )
{
  EW_UNUSED_ARG(aGuiWidth);
  EW_UNUSED_ARG(aGuiHeight);
  void* fb1;
  void* fb2;

  display_handles_t handles;

  /* check and clean display info structure */
  if ( !aDisplayInfo )
    return 0;
  memset( aDisplayInfo, 0, sizeof( XDisplayInfo ));

  #if EW_USE_OPERATING_SYSTEM == 1

    /* create the LCD update semaphore */
    LcdUpdateSemaphore = EwBspOsSemaphoreCreate( 1, 1 );

  #endif

  /* Initialize display brightness control */
  if ( EwBspDisplayBrightnessInit() != ESP_OK )
  {
    EwPrint( "EwBspDisplayInit: EwBspDisplayBrightnessInit() failed.\n" );
    return 0;
  }

  /* Initialize LCD display */
  if ( EwBspDisplayLcdInit( &handles ) != ESP_OK )
  {
    EwPrint( "EwBspDisplayInit: EwBspDisplayLcdInit() failed.\n" );
    return 0;
  }

  PanelHandle = handles.panel;

  /* Turn on the display */
  if ( esp_lcd_panel_disp_on_off( PanelHandle, true ) != ESP_OK )
  {
    EwPrint( "EwBspDisplayInit: esp_lcd_panel_disp_on_off() failed.\n" );
    return 0;
  }

  if ( esp_lcd_dpi_panel_get_frame_buffer( PanelHandle, 2, &fb1, &fb2 ) != ESP_OK )
  {
    EwPrint( "EwBspDisplayInit: esp_lcd_dpi_panel_get_frame_buffer() failed.\n" );
    return 0;
  }

  esp_lcd_dpi_panel_event_callbacks_t cbs = {
    .on_refresh_done = VSyncCallback,
  };

  if ( esp_lcd_dpi_panel_register_event_callbacks( PanelHandle, &cbs, 0 ) != ESP_OK )
  {
    EwPrint( "EwBspDisplayInit: esp_lcd_dpi_panel_register_event_callbacks() failed.\n" );
    return 0;
  }

  #if EW_USE_OPERATING_SYSTEM == 1

    /* initially take the LcdUpdate token for the first LCD update */
    EwBspOsSemaphoreWait( LcdUpdateSemaphore, 1000 );

  #endif

  /* return the current display configuration */
  aDisplayInfo->FrameBuffer   = fb1;
  aDisplayInfo->DoubleBuffer  = fb2;
  aDisplayInfo->BufferWidth   = EW_FRAME_BUFFER_WIDTH;
  aDisplayInfo->BufferHeight  = EW_FRAME_BUFFER_HEIGHT;
  aDisplayInfo->DisplayWidth  = EW_DISPLAY_WIDTH;
  aDisplayInfo->DisplayHeight = EW_DISPLAY_HEIGHT;
  aDisplayInfo->UpdateMode    = EW_BSP_DISPLAY_UPDATE_NORMAL;

  /* switch on the LCD backlight to 50% */
  EwBspDisplayBrightnessSet( 50 );

  return 1;
}


/*******************************************************************************
* FUNCTION:
*   EwBspDisplayDone
*
* DESCRIPTION:
*   The function EwBspDisplayDone deinitializes the display hardware.
*
* ARGUMENTS:
*   aDisplayInfo - Display info data structure.
*
* RETURN VALUE:
*   None
*
*******************************************************************************/
void EwBspDisplayDone( XDisplayInfo* aDisplayInfo )
{
  EW_UNUSED_ARG( aDisplayInfo );

  #if EW_USE_OPERATING_SYSTEM == 1

    /* destroy the LCD semaphore */
    EwBspOsSemaphoreDestroy( LcdUpdateSemaphore );

  #endif
}


/*******************************************************************************
* FUNCTION:
*   EwBspDisplayGetUpdateArea
*
* DESCRIPTION:
*   The function EwBspDisplayGetUpdateArea returns the next update area
*   depending on the selected display mode:
*   In case of a synchroneous single-buffer, the function has to return the
*   the rectangular areas that correspond to the horizontal stripes (fields)
*   of the framebuffer.
*   In case of a scratch-pad buffer, the function has to return the subareas
*   that fit into the provided update rectangle.
*   During each display update, this function is called until it returns 0.
*
* ARGUMENTS:
*   aUpdateRect - Rectangular area which should be updated (redrawn).
*
* RETURN VALUE:
*   Returns 1 if a further update area can be provided, 0 otherwise.
*
*******************************************************************************/
int EwBspDisplayGetUpdateArea( XRect* aUpdateRect )
{
  return 0;
}


/*******************************************************************************
* FUNCTION:
*   EwBspDisplayWaitForCompletion
*
* DESCRIPTION:
*   The function EwBspDisplayWaitForCompletion is called from the Graphics Engine
*   to ensure that all pending activities of the display system are completed, so
*   that the rendering of the next frame can start.
*   In case of a double-buffering system, the function has to wait until the
*   V-sync has occured and the pending buffer is used by the display controller.
*   In case of an external display controller, the function has to wait until
*   the transfer (update) of the graphics data has been completed and there are
*   no pending buffers.
*
* ARGUMENTS:
*   None
*
* RETURN VALUE:
*   None
*
*******************************************************************************/
void EwBspDisplayWaitForCompletion( void )
{
  register uint32_t pendingBuffer = PendingFramebuffer;
  if ( CurrentFramebuffer == pendingBuffer )
    return;

  #if EW_USE_OPERATING_SYSTEM == 1

    /* wait until pending framebuffer is used as current framebuffer and
       use CPU time for other tasks */
    EwBspOsSemaphoreWait( LcdUpdateSemaphore, 1000 );
    CurrentFramebuffer = pendingBuffer;

  #else

    /* wait until pending framebuffer is used as current framebuffer */
    while( CurrentFramebuffer != pendingBuffer )
      ;

  #endif
}


/*******************************************************************************
* FUNCTION:
*   EwBspDisplayCommitBuffer
*
* DESCRIPTION:
*   The function EwBspDisplayCommitBuffer is called from the Graphics Engine
*   when the rendering of a certain buffer has been completed.
*   The type of buffer depends on the selected framebuffer concept.
*   If the display is running in a double-buffering mode, the function is called
*   after each buffer update in order to change the currently active framebuffer
*   address. Changing the framebuffer address should be synchronized with V-sync.
*   If the system is using an external graphics controller, this function is
*   responsible to start the transfer of the framebuffer content.
*
* ARGUMENTS:
*   aAddress - Address of the framebuffer to be shown on the display.
*   aX,
*   aY       - Origin of the area which has been updated by the Graphics Engine.
*   aWidth,
*   aHeight  - Size of the area which has been updated by the Graphics Engine.
*
* RETURN VALUE:
*   None
*
*******************************************************************************/
void EwBspDisplayCommitBuffer( void* aAddress, int aX, int aY, int aWidth, int aHeight )
{
  /* set pending framebuffer address to be used on next V-sync */
  PendingFramebuffer = (uint32_t)aAddress;

  if ( esp_lcd_panel_draw_bitmap( PanelHandle, aX, aY, aX + aWidth, aY + aHeight, aAddress ) != ESP_OK )
    EwPrint("EwBspDisplayCommitBuffer: esp_lcd_panel_draw_bitmap() failed.\n");
}


/*******************************************************************************
* FUNCTION:
*   EwBspDisplaySetClut
*
* DESCRIPTION:
*   The function EwBspDisplaySetClut is called from the Graphics Engine
*   in order to update the hardware CLUT of the current framebuffer.
*   The function is only called when the color format of the framebuffer is
*   Index8 or LumA44.
*
* ARGUMENTS:
*   aClut - Pointer to a color lookup table with 256 entries.
*
* RETURN VALUE:
*   None
*
*******************************************************************************/
void EwBspDisplaySetClut( unsigned long* aClut )
{
}


/* msy */
