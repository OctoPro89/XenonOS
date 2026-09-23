#include <drivers/usb/xhci_trb.h>

const char* xhci_trb_completion_code_to_string(u8 completion_code) {
    switch (completion_code) {
        case XHCI_TRB_COMPLETION_CODE_INVALID:
            return "INVALID";
        case XHCI_TRB_COMPLETION_CODE_SUCCESS:
            return "SUCCESS";
        case XHCI_TRB_COMPLETION_CODE_DATA_BUFFER_ERROR:
            return "DATA_BUFFER_ERROR";
        case XHCI_TRB_COMPLETION_CODE_BABBLE_DETECTED_ERROR:
            return "BABBLE_DETECTED_ERROR";
        case XHCI_TRB_COMPLETION_CODE_USB_TRANSACTION_ERROR:
            return "USB_TRANSACTION_ERROR";
        case XHCI_TRB_COMPLETION_CODE_TRB_ERROR:
            return "TRB_ERROR";
        case XHCI_TRB_COMPLETION_CODE_STALL_ERROR:
            return "STALL_ERROR";
        case XHCI_TRB_COMPLETION_CODE_RESOURCE_ERROR:
            return "RESOURCE_ERROR";
        case XHCI_TRB_COMPLETION_CODE_BANDWIDTH_ERROR:
            return "BANDWIDTH_ERROR";
        case XHCI_TRB_COMPLETION_CODE_NO_SLOTS_AVAILABLE:
            return "NO_SLOTS_AVAILABLE";
        case XHCI_TRB_COMPLETION_CODE_INVALID_STREAM_TYPE:
            return "INVALID_STREAM_TYPE";
        case XHCI_TRB_COMPLETION_CODE_SLOT_NOT_ENABLED:
            return "SLOT_NOT_ENABLED";
        case XHCI_TRB_COMPLETION_CODE_ENDPOINT_NOT_ENABLED:
            return "ENDPOINT_NOT_ENABLED";
        case XHCI_TRB_COMPLETION_CODE_SHORT_PACKET:
            return "SHORT_PACKET";
        case XHCI_TRB_COMPLETION_CODE_RING_UNDERRUN:
            return "RING_UNDERRUN";
        case XHCI_TRB_COMPLETION_CODE_RING_OVERRUN:
            return "RING_OVERRUN";
        case XHCI_TRB_COMPLETION_CODE_VF_EVENT_RING_FULL:
            return "VF_EVENT_RING_FULL";
        case XHCI_TRB_COMPLETION_CODE_PARAMETER_ERROR:
            return "PARAMETER_ERROR";
        case XHCI_TRB_COMPLETION_CODE_BANDWIDTH_OVERRUN:
            return "BANDWIDTH_OVERRUN";
        case XHCI_TRB_COMPLETION_CODE_CONTEXT_STATE_ERROR:
            return "CONTEXT_STATE_ERROR";
        case XHCI_TRB_COMPLETION_CODE_NO_PING_RESPONSE:
            return "NO_PING_RESPONSE";
        case XHCI_TRB_COMPLETION_CODE_EVENT_RING_FULL:
            return "EVENT_RING_FULL";
        case XHCI_TRB_COMPLETION_CODE_INCOMPATIBLE_DEVICE:
            return "INCOMPATIBLE_DEVICE";
        case XHCI_TRB_COMPLETION_CODE_MISSED_SERVICE:
            return "MISSED_SERVICE";
        case XHCI_TRB_COMPLETION_CODE_COMMAND_RING_STOPPED:
            return "COMMAND_RING_STOPPED";
        case XHCI_TRB_COMPLETION_CODE_COMMAND_ABORTED:
            return "COMMAND_ABORTED";
        case XHCI_TRB_COMPLETION_CODE_STOPPED:
            return "STOPPED";
        case XHCI_TRB_COMPLETION_CODE_STOPPED_LENGTH_INVALID:
            return "STOPPED_LENGTH_INVALID";
        case XHCI_TRB_COMPLETION_CODE_STOPPED_SHORT_PACKET:
            return "STOPPED_SHORT_PACKET";
        case XHCI_TRB_COMPLETION_CODE_MAX_EXIT_LATENCY_ERROR:
            return "MAX_EXIT_LATENCY_ERROR";
        default:
            return "UNKNOWN_COMPLETION_CODE";
    }
}