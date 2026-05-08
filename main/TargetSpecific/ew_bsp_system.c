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
*   This template is responsible to configurate the entire system (CPU clock,
*   memory, qspi, etc).
*
*******************************************************************************/

#include "ewconfig.h"
#include "ew_bsp_system.h"

#include "esp_heap_caps.h"
#include "esp_cache.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

// I2C configuration (shared bus for display and touch)
#define SYSTEM_I2C_NUM          (0)
#define SYSTEM_I2C_SDA_GPIO     (7)
#define SYSTEM_I2C_SCL_GPIO     (8)
#define SYSTEM_I2C_CLK_SPEED_HZ (400000)

static const char *TAG = "EW_BSP_SYSTEM";
static i2c_master_bus_handle_t I2CHandle = NULL;

/*******************************************************************************
* FUNCTION:
*   EwBspSystemInit
*
* DESCRIPTION:
*   The function EwBspSystemInit initializes the system components.
*   (CPU clock, memory, qspi, I2C bus, ...)
*
* ARGUMENTS:
*   None
*
* RETURN VALUE:
*   None
*
*******************************************************************************/
void EwBspSystemInit( void )
{
  /* Initialize I2C bus (shared by display and touch) */
  i2c_master_bus_config_t i2c_bus_conf = {
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .sda_io_num = SYSTEM_I2C_SDA_GPIO,
      .scl_io_num = SYSTEM_I2C_SCL_GPIO,
      .i2c_port = SYSTEM_I2C_NUM,
      .flags.enable_internal_pullup = true,
  };

  if (i2c_new_master_bus(&i2c_bus_conf, &I2CHandle) != ESP_OK)
  {
      ESP_LOGE(TAG, "Failed to initialize I2C bus");
  }
  else
  {
      ESP_LOGI(TAG, "I2C bus initialized successfully");
  }
}


/*******************************************************************************
* FUNCTION:
*   EwBspSystemDone
*
* DESCRIPTION:
*   The function EwBspSystemDone terminates the system components.
*
* ARGUMENTS:
*   None
*
* RETURN VALUE:
*   None
*
*******************************************************************************/
void EwBspSystemDone( void )
{
}


/*******************************************************************************
* FUNCTION:
*   EwBspSystemInvalidateCache
*
* DESCRIPTION:
*   The function EwBspSystemInvalidateCache is called from the Graphics Engine
*   to invalidate the data cache for a certain memory area defined by the
*   parameters aAddress and aSize.
*   The function is called after the GPU has modified a memory area and before
*   the CPU is accessing this memory area. In this case, cache invalidation
*   ensures that the cache does not contain old content.
*
* ARGUMENTS:
*   aAddress       - Startaddress of the memory area to invalidate in data cache.
*   aSize t        - Size of the memory area in bytes.
*
* RETURN VALUE:
*   None
*
*******************************************************************************/
void EwBspSystemInvalidateCache( void* aAddress, unsigned int aSize )
{
  esp_cache_msync( aAddress, ( aSize + 63 ) & ~63, ESP_CACHE_MSYNC_FLAG_DIR_M2C );
}


/*******************************************************************************
* FUNCTION:
*   EwBspSystemCleanCache
*
* DESCRIPTION:
*   The function EwBspSystemCleanCache is called from the Graphics Engine
*   to clean (flush) the data cache for a certain memory area defined by the
*   parameters aAddress and aSize.
*   The function is called after the CPU has modified a memory area and before
*   the GPU or the display controller is reading this memory area. In this case,
*   cache cleaning ensures that the cache does not keep prepared data.
*
* ARGUMENTS:
*   aAddress       - Startaddress of the memory area to invalidate in data cache.
*   aSize t        - Size of the memory area in bytes.
*
* RETURN VALUE:
*   None
*
*******************************************************************************/
void EwBspSystemCleanCache( void* aAddress, unsigned int aSize )
{
  esp_cache_msync( aAddress, aSize, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED );
}


/*******************************************************************************
* FUNCTION:
*   EwBspSystemGetI2CHandle
*
* DESCRIPTION:
*   Get the shared I2C bus handle (used by display and touch).
*
* ARGUMENTS:
*   None
*
* RETURN VALUE:
*   Returns the I2C master bus handle, or NULL if not initialized.
*
*******************************************************************************/
i2c_master_bus_handle_t EwBspSystemGetI2CHandle( void )
{
  return I2CHandle;
}


/* msy */
