module m #(
    parameter NUM_CONSUMERS = 2,
    parameter NUM_CHANNELS = 4
) (
    input wire [NUM_CONSUMERS-1:0]  consumer_read_valid
);

    logic controller_state [NUM_CHANNELS-1:0];
    logic controller_state_next [NUM_CHANNELS-1:0];

    always_comb begin

        controller_state_next = controller_state;

        for (int i = 0; i < NUM_CHANNELS; i = i + 1) begin
                    for (int j = 0; j < NUM_CONSUMERS; j = j + 1) begin
                        if (consumer_read_valid[j]) begin
                            controller_state_next[i] = 1;
                        end
                    end
        end
    end

endmodule
