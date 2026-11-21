#pragma once

// Pinagem oficial da placa Guition JC3248W535C (ESP32-S3 + AXS15231B 3.5" 320x480)
// Permite sobrescrita via build_flags no platformio.ini

#ifndef GFX_BL
#define GFX_BL 1
#endif

// Barramento QSPI do Display AXS15231B
#ifndef QSPI_CS
#define QSPI_CS 45
#endif
#ifndef QSPI_SCLK
#define QSPI_SCLK 47
#endif
#ifndef QSPI_D0
#define QSPI_D0 21
#endif
#ifndef QSPI_D1
#define QSPI_D1 48
#endif
#ifndef QSPI_D2
#define QSPI_D2 40
#endif
#ifndef QSPI_D3
#define QSPI_D3 39
#endif

// Dimensões nativas do painel (retrato)
constexpr int16_t PANEL_NATIVE_WIDTH = 320;
constexpr int16_t PANEL_NATIVE_HEIGHT = 480;

// Rotação padrão do Canvas: 1 ou 3 = Paisagem (480x320)
#ifndef DISPLAY_ROTATION
#define DISPLAY_ROTATION 1
#endif

// Touch Capacitivo AXS15231B (I2C)
#ifndef TOUCH_SDA
#define TOUCH_SDA 4
#endif
#ifndef TOUCH_SCL
#define TOUCH_SCL 8
#endif
#ifndef TOUCH_INT
#define TOUCH_INT 3
#endif
#ifndef TOUCH_ADDR
#define TOUCH_ADDR 0x3B
#endif
