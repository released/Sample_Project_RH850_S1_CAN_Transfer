/* Project/API reading notes: Project-owned initialization and foreground work.
 * The selected main/startup adapter calls hardware_init() once and loop() repeatedly.
 * Keep application changes here so generated entry sources remain replaceable; lower-level APIs
 * live in this project's hal directories.
 */
#include "r_smc_entry.h"
#include "custom_func.h"
#include "retarget.h"
#include "can_transfer_config.h"
#include "can_transfer_protocol.h"
#include "rh850_can.h"
#include "rh850_can_ports.h"

static volatile uint32_t g_millis;
static ct_protocol_t g_protocol[6];
static uint8_t g_ready;
static uint8_t g_previous_error_state[6];
static uint16_t g_drop_baseline[6];
static uint16_t g_invalid_seen[6];
volatile UART_MANAGER_T UART0Manager;

unsigned long Device_Millis(void)
{
    return g_millis;
}
/* Advance the monotonic timebase only.
 * Protocol timeout, application work and text output are handled outside this ISR.
 */
void ostmr_1ms_IRQ(void)
{
    g_millis++;
}
void tmr_1ms_IRQ(void)
{
}
void UARTx_Process(unsigned char value)
{
    (void)value;
}
void UARTx_ErrorCheckProcess(unsigned char value)
{
    (void)value;
}

/* UART is diagnostics only. Never wait without a bound, including no debugger. */
/* Foreground-only diagnostic output used by retarget.c.
 * Preserve a finite wait when UART0 is busy or stopped; ISP data uses its own link.
 */
int SendChar(unsigned char value)
{
    unsigned long guard;
    guard = 100000UL;
    while ((RLN30.LST & _UART_TRANSMISSION_OPERATED) && guard)
    {
        guard--;
    }
    if (guard)
    {
        RLN30.LUTDR.UINT16 = value;
        return (int)value;
    }
    return -1;
}
static void Trace(const char *direction, uint16_t id, const uint8_t *data)
{
#if CAN_TRANSFER_UART_TRACE
    uint8_t i;
    tiny_printf("TRACE CT CAN%X %s id=0x%03X dlc=8 data=[",
                (unsigned int)Device_CanPort(), direction, (unsigned int)id);
    for (i = 0U; i < 8U; i++)
    {
        if (i != 0U)
        {
            tiny_printf(" ");
        }
        tiny_printf("%02X", (unsigned int)data[i]);
    }
    tiny_printf("] crc=%s\r\n", (CT_Crc8(data, 7U) == data[7]) ? "OK" : "BAD");
#else
    (void)direction;
    (void)id;
    (void)data;
#endif
}

/* Initialize the role selected by this project before foreground processing.
 * Preserve clock, image/handoff and peripheral/IRQ enable order when adding application
 * initialization.
 */
void hardware_init(void)
{
    uint8_t channel;
    for (channel = 0U; channel < 6U; channel++)
    {
        CT_Init(&g_protocol[channel]);
    }
    g_millis = 0UL;
    DI();
    R_Config_UART0_Start();
    /* The diagnostic UART is polled TX only; no RX address is armed. */
    INTC2.ICRLIN30UR0.BIT.MKRLIN30UR0 = 1U;
    INTC2.ICRLIN30UR1.BIT.MKRLIN30UR1 = 1U;
    INTC2.ICRLIN30UR2.BIT.MKRLIN30UR2 = 1U;
    EI();
    R_Config_OSTM0_Start();
    g_ready = Device_CanInit(CAN_TRANSFER_REQUEST_ID);
    tiny_printf("\r\nRH850 CAN DEVICE TRANSFER: EVM R7F701684 / X1=16M\r\n");
    tiny_printf("CPU=120M fCAN=40M; enabled CAN ports:");
    for (channel = 0U; channel < 6U; channel++)
    {
        if (CanPorts_Enabled(channel))
        {
            tiny_printf(" ");
            tiny_printf("%X", (unsigned int)channel);
        }
    }
    tiny_printf("\r\nClassical 500K request=0x710 response=0x711; per-port FIFO IRQ\r\n");
    tiny_printf("%s", (g_ready != 0U) ? "CAN init: OK\r\n" : "CAN init: FAILED\r\n");
}

/* Foreground service point called repeatedly by the selected main adapter.
 * Keep protocol/application work and text logging bounded so pending traffic and deferred actions
 * can continue.
 */
void loop(void)
{
    device_can_frame_t frame;
    device_can_diagnostics_t diag;
    ct_protocol_t *protocol;
    uint8_t response[8], channel, errors;
    uint32_t status;
    uint16_t invalid;
    if (g_ready == 0U)
    {
        return;
    }
    for (channel = 0U; channel < 6U; channel++)
    {
        if (!CanPorts_Enabled(channel))
        {
            continue;
        }
        protocol = &g_protocol[channel];
        status = CanPorts_Status(channel);
        errors = 0U;
        if (((status >> 8) & 255U) >= 96U || ((status >> 16) & 255U) >= 96U)
        {
            errors |= 1U;
        }
        if (status & 16UL)
        {
            errors |= 2U;
        }
        if (status & 8UL)
        {
            errors |= 4U;
        }
        if ((errors & 1U) && !(g_previous_error_state[channel] & 1U))
        {
            CT_Increment(&protocol->stats[7]);
        }
        if ((errors & 2U) && !(g_previous_error_state[channel] & 2U))
        {
            CT_Increment(&protocol->stats[8]);
        }
        if ((errors & 4U) && !(g_previous_error_state[channel] & 4U))
        {
            CT_Increment(&protocol->stats[9]);
        }
        g_previous_error_state[channel] = errors;
    }
    if (!Device_CanRead(&frame))
    {
        return;
    }
    channel = frame.channel;
    protocol = &g_protocol[channel];
    Device_CanDiagnostics(&diag);
    protocol->stats[2] = (uint16_t)(diag.dropped - g_drop_baseline[channel]);
    invalid = (uint16_t)(diag.invalid - g_invalid_seen[channel]);
    g_invalid_seen[channel] = diag.invalid;
    while (invalid)
    {
        CT_Increment(&protocol->stats[4]);
        invalid--;
    }
    protocol->stats[10] = diag.last_result;
    protocol->stats[11] = (uint16_t)CanPorts_ErrorFlags(channel);
    if (frame.id != CAN_TRANSFER_REQUEST_ID)
    {
        return;
    }
    Trace("RX", CAN_TRANSFER_REQUEST_ID, frame.data);
    CT_Process(protocol, frame.data, response);
    if (response[3] == 6U && frame.data[3] == 6U && !protocol->stats[6])
    {
        g_drop_baseline[channel] = diag.dropped;
    }
    if (Device_CanSend(CAN_TRANSFER_RESPONSE_ID, response))
    {
        CT_Increment(&protocol->stats[1]);
        Trace("TX", CAN_TRANSFER_RESPONSE_ID, response);
    }
    else
    {
        CT_Increment(&protocol->stats[3]);
    }
}
