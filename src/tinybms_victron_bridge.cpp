/**
 * @file tinybms_victron_bridge.cpp
 * @brief Bridge aggregation unit - see modular implementation files.
 *
 * This translation unit exists to preserve the original include path
 * while the bridge implementation has been split across dedicated
 * modules:
 *   - bridge_core.cpp       : constructor, begin(), task creation helpers
 *   - bridge_uart.cpp       : TinyBMS polling over UART
 *   - bridge_can.cpp        : Victron PGN builders + CAN task
 *   - bridge_cvl.cpp        : CVL supervision task
 *   - bridge_keepalive.cpp  : VE.Can keepalive management
 *
 * Including their headers here keeps backwards compatibility for any
 * TU that previously only depended on tinybms_victron_bridge.cpp, while
 * the actual logic now lives in the modular sources above.
 */

#include "bridge_core.h"
#include "bridge_uart.h"
#include "bridge_can.h"
#include "bridge_cvl.h"
#include "bridge_keepalive.h"
