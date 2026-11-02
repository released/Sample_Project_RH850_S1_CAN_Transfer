#include <string.h>
#include "r_smc_entry.h"
#include "rh850_can.h"
#include "rh850_can_ports.h"

static unsigned char g_reply_port;
static unsigned short g_bad_length[6];
uint8_t Device_CanInit(uint16_t request_id)
{
    memset(g_bad_length, 0, sizeof(g_bad_length));
    g_reply_port = CanPorts_First();
    return CanPorts_Init(0U, request_id, 0U, 0U, 0U);
}
#pragma interrupt rs_canfd_rx_fifo_interrupt(enable = false, channel = 23, fpu = true,             \
                                             callt = false)
void rs_canfd_rx_fifo_interrupt(void)
{
    CanPorts_Irq();
}
uint8_t Device_CanRead(device_can_frame_t *frame)
{
    can_port_frame_t received;
    if (!frame || !CanPorts_Read(&received))
    {
        return 0U;
    }
    if (received.length != 8U)
    {
        if (g_bad_length[received.channel] != 65535U)
        {
            g_bad_length[received.channel]++;
        }
        return 0U;
    }
    frame->id = received.id;
    frame->channel = received.channel;
    memcpy(frame->data, received.data, 8U);
    g_reply_port = received.channel;
    return 1U;
}
uint8_t Device_CanSend(uint16_t id, const uint8_t data[8])
{
    return CanPorts_Send(g_reply_port, id, data, 8U);
}
unsigned char Device_CanPort(void)
{
    return g_reply_port;
}
void Device_CanDiagnostics(device_can_diagnostics_t *out)
{
    can_port_diagnostics_t diagnostic;
    unsigned long invalid;
    if (!out)
    {
        return;
    }
    CanPorts_Diagnostics(g_reply_port, &diagnostic);
    invalid = (unsigned long)diagnostic.invalid + g_bad_length[g_reply_port];
    out->irq = diagnostic.irq;
    out->dropped = diagnostic.dropped;
    out->invalid = (uint16_t)((invalid > 65535UL) ? 65535UL : invalid);
    out->api_errors = diagnostic.api_errors;
    out->last_result = diagnostic.last_result;
    out->peak = diagnostic.peak;
}
void Device_CanStop(void)
{
    CanPorts_Stop();
}
