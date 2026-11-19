#include "control_state.h"

static bool active_phase(enum control_phase phase)
{
    return phase == CONTROL_RUNNING || phase == CONTROL_BRAKING;
}

void chassis_control_note_tx(struct chassis_control_state *state, uint32_t now, bool nonzero)
{
    state->tx_started_ms = now;
    state->tx_active = true;
    state->tx_nonzero = nonzero;
    state->tx_recovery_zero = state->phase == CONTROL_RECOVERING && !nonzero;
    state->tx_cancel_requested = false;
    state->tx_timed_out = false;
}

static void update_phase(struct chassis_control_state *state,
                         const struct chassis_control_input *input, bool remote_interrupted)
{
    if (state->phase == CONTROL_RECOVERING)
    {
        if (input->now - state->recovery_started_ms >= CHASSIS_RECOVERY_WAIT_MS &&
            input->feedback_fresh && state->recovery_zero_frames >= CHASSIS_READY_FRAMES)
        {
            state->phase = CONTROL_WAITING;
            state->ready_for_center = false;
        }
        return;
    }
    if (!input->hardware_ready || !input->remote_valid || remote_interrupted)
    {
        state->phase = CONTROL_WAITING;
        state->ready_for_center = false;
        return;
    }

    switch (state->phase)
    {
        case CONTROL_WAITING:
            if (!state->ready_for_center)
            {
                state->wait_frame = input->remote_frame;
                state->ready_for_center = true;
            }
            else if (input->remote_frame != state->wait_frame && input->centered)
            {
                state->phase = CONTROL_ARMED;
            }
            break;
        case CONTROL_ARMED:
            if (!input->centered)
            {
                state->phase = CONTROL_RUNNING;
            }
            break;
        case CONTROL_RUNNING:
            if (input->centered)
            {
                state->phase = input->wheels_stopped ? CONTROL_ARMED : CONTROL_BRAKING;
            }
            break;
        case CONTROL_BRAKING:
            if (!input->centered)
            {
                state->phase = CONTROL_RUNNING;
            }
            else if (input->wheels_stopped)
            {
                state->phase = CONTROL_ARMED;
            }
            break;
        default:
            break;
    }
}

struct chassis_control_output chassis_control_step(struct chassis_control_state *state,
                                                  const struct chassis_control_input *input)
{
    struct chassis_control_output output = {0};
    enum control_phase previous = state->phase;
    bool remote_interrupted = state->remote_interruptions != input->remote_interruptions;
    state->remote_interruptions = input->remote_interruptions;
    output.remote_stop = (active_phase(previous) || previous == CONTROL_ARMED) &&
                         (!input->remote_valid || remote_interrupted);

    if (state->tx_active && !input->tx_pending)
    {
        output.tx_completed = input->tx_sent;
        output.tx_failed = !input->tx_sent && !state->tx_cancel_requested;
        if (input->tx_sent && state->tx_recovery_zero && !state->tx_cancel_requested &&
            state->recovery_zero_frames < CHASSIS_READY_FRAMES)
        {
            ++state->recovery_zero_frames;
        }
        state->tx_active = false;
    }
    output.tx_timeout = state->tx_active && !state->tx_timed_out &&
                        input->now - state->tx_started_ms >= CHASSIS_TX_TIMEOUT_MS;
    if (output.tx_timeout)
    {
        state->tx_timed_out = true;
    }

    enum recovery_reason fault = input->can_fault;
    if (fault == RECOVERY_NONE && output.tx_timeout)
    {
        fault = RECOVERY_TX_TIMEOUT;
    }
    bool needs_fresh_feedback = previous == CONTROL_BRAKING ||
                                (previous == CONTROL_RUNNING && input->centered);
    if (fault == RECOVERY_NONE && active_phase(previous) &&
        (!input->feedback_ready || (needs_fresh_feedback && !input->feedback_fresh)))
    {
        fault = RECOVERY_STALE_FEEDBACK;
    }

    if (fault != RECOVERY_NONE)
    {
        output.recovery_started = previous != CONTROL_RECOVERING;
        state->phase = CONTROL_RECOVERING;
        state->recovery_reason = fault;
        state->recovery_started_ms = input->now;
        state->recovery_zero_frames = 0;
        state->tx_recovery_zero = false;
    }
    else
    {
        update_phase(state, input, remote_interrupted);
    }

    bool leaving_active_phase = active_phase(previous) && previous != state->phase;
    output.cancel_tx = input->tx_pending && !state->tx_cancel_requested &&
                       (output.tx_timeout || (state->tx_nonzero && leaving_active_phase));
    if (output.cancel_tx)
    {
        state->tx_cancel_requested = true;
    }
    return output;
}
