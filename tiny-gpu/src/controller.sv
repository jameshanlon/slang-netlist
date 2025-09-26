`default_nettype none
`timescale 1ns/1ns

// MEMORY CONTROLLER
// > Receives memory requests from all cores
// > Throttles requests based on limited external memory bandwidth
// > Waits for responses from external memory and distributes them back to cores
module controller #(
    parameter ADDR_BITS = 8,
    parameter DATA_BITS = 16,
    parameter NUM_CONSUMERS = 4, // The number of consumers accessing memory through this controller
    parameter NUM_CHANNELS = 1,  // The number of concurrent channels available to send requests to global memory
    parameter WRITE_ENABLE = 1   // Whether this memory controller can write to memory (program memory is read-only)
) (
    input wire clk,
    input wire reset,

    // Consumer Interface (Fetchers / LSUs)
    input wire [NUM_CONSUMERS-1:0]  consumer_read_valid,
    input wire [ADDR_BITS-1:0]      consumer_read_address [NUM_CONSUMERS-1:0],
    output logic [NUM_CONSUMERS-1:0] consumer_read_ready,
    output logic [DATA_BITS-1:0]     consumer_read_data [NUM_CONSUMERS-1:0],
    input wire [NUM_CONSUMERS-1:0]  consumer_write_valid,
    input wire [ADDR_BITS-1:0]      consumer_write_address [NUM_CONSUMERS-1:0],
    input wire [DATA_BITS-1:0]      consumer_write_data [NUM_CONSUMERS-1:0],
    output logic [NUM_CONSUMERS-1:0] consumer_write_ready,

    // Memory Interface (Data / Program)
    output logic [NUM_CHANNELS-1:0] mem_read_valid,
    output logic [ADDR_BITS-1:0]    mem_read_address [NUM_CHANNELS-1:0],
    input wire [NUM_CHANNELS-1:0]  mem_read_ready,
    input wire [DATA_BITS-1:0]     mem_read_data [NUM_CHANNELS-1:0],
    output logic [NUM_CHANNELS-1:0] mem_write_valid,
    output logic [ADDR_BITS-1:0]    mem_write_address [NUM_CHANNELS-1:0],
    output logic [DATA_BITS-1:0]    mem_write_data [NUM_CHANNELS-1:0],
    input wire [NUM_CHANNELS-1:0]  mem_write_ready
);
    localparam [2:0] IDLE           = 3'b000,
                     READ_WAITING   = 3'b010,
                     WRITE_WAITING  = 3'b011,
                     READ_RELAYING  = 3'b100,
                     WRITE_RELAYING = 3'b101;

    // Registers for the state of each channel
    logic [2:0] controller_state [NUM_CHANNELS-1:0];
    logic [$clog2(NUM_CONSUMERS)-1:0] current_consumer [NUM_CHANNELS-1:0];
    logic [NUM_CONSUMERS-1:0] channel_serving_consumer;

    // Wires for next-state logic, connecting the combinational and sequential blocks
    logic [2:0] controller_state_next [NUM_CHANNELS-1:0];
    logic [$clog2(NUM_CONSUMERS)-1:0] current_consumer_next [NUM_CHANNELS-1:0];
    logic [NUM_CONSUMERS-1:0] channel_serving_consumer_next;


    // --- Combinational Logic for Next State and Outputs ---
    always_comb begin
        // Local variable for arbitration. Declaration MUST be first.
        logic [NUM_CONSUMERS-1:0] consumer_available_for_service;

        // Default assignments: hold current values unless changed below
        controller_state_next = controller_state;
        current_consumer_next = current_consumer;
        channel_serving_consumer_next = channel_serving_consumer;

        // Default assignments for outputs
        mem_read_valid = '0;
        mem_read_address = '{default:'0};
        mem_write_valid = '0;
        mem_write_address = '{default:'0};
        mem_write_data = '{default:'0};
        consumer_read_ready = '0;
        consumer_read_data = '{default:'0};
        consumer_write_ready = '0;

        // This variable tracks which consumers are not currently being served.
        consumer_available_for_service = ~channel_serving_consumer;

        // For each channel, we handle processing concurrently
        for (int i = 0; i < NUM_CHANNELS; i = i + 1) begin
            case (controller_state[i])
                IDLE: begin
                    // While this channel is idle, cycle through consumers looking for a request
                    for (int j = 0; j < NUM_CONSUMERS; j = j + 1) begin
                        if (consumer_read_valid[j] && consumer_available_for_service[j]) begin
                            channel_serving_consumer_next[j] = 1'b1; // Mark consumer as taken
                            consumer_available_for_service[j] = 1'b0; // Prevent other channels from taking it this cycle

                            current_consumer_next[i] = j;
                            mem_read_valid[i] = 1'b1;
                            mem_read_address[i] = consumer_read_address[j];
                            controller_state_next[i] = READ_WAITING;
                            break; // Channel has found work, stop searching
                        end
                        else if (WRITE_ENABLE && consumer_write_valid[j] && consumer_available_for_service[j]) begin
                            channel_serving_consumer_next[j] = 1'b1; // Mark consumer as taken
                            consumer_available_for_service[j] = 1'b0; // Prevent other channels from taking it this cycle
                           
                            current_consumer_next[i] = j;
                            mem_write_valid[i] = 1'b1;
                            mem_write_address[i] = consumer_write_address[j];
                            mem_write_data[i] = consumer_write_data[j];
                            controller_state_next[i] = WRITE_WAITING;
                            break; // Channel has found work, stop searching
                        end
                    end
                end

                READ_WAITING: begin
                    mem_read_valid[i] = 1'b1; // Keep valid high until memory is ready
                    mem_read_address[i] = consumer_read_address[current_consumer[i]];

                    if (mem_read_ready[i]) begin
                        mem_read_valid[i] = 1'b0;
                        consumer_read_ready[current_consumer[i]] = 1'b1;
                        consumer_read_data[current_consumer[i]] = mem_read_data[i];
                        controller_state_next[i] = READ_RELAYING;
                    end
                end

                WRITE_WAITING: begin
                    mem_write_valid[i] = 1'b1; // Keep valid high until memory is ready
                    mem_write_address[i] = consumer_write_address[current_consumer[i]];
                    mem_write_data[i] = consumer_write_data[current_consumer[i]];

                    if (mem_write_ready[i]) begin
                        mem_write_valid[i] = 1'b0;
                        consumer_write_ready[current_consumer[i]] = 1'b1;
                        controller_state_next[i] = WRITE_RELAYING;
                    end
                end

                READ_RELAYING: begin
                    consumer_read_ready[current_consumer[i]] = 1'b1; // Keep ready high
                    consumer_read_data[current_consumer[i]] = mem_read_data[i]; // Hold data

                    if (!consumer_read_valid[current_consumer[i]]) begin
                        channel_serving_consumer_next[current_consumer[i]] = 1'b0; // Free up the consumer
                        consumer_read_ready[current_consumer[i]] = 1'b0;
                        controller_state_next[i] = IDLE;
                    end
                end

                WRITE_RELAYING: begin
                    consumer_write_ready[current_consumer[i]] = 1'b1; // Keep ready high
                    if (!consumer_write_valid[current_consumer[i]]) begin
                        channel_serving_consumer_next[current_consumer[i]] = 1'b0; // Free up the consumer
                        consumer_write_ready[current_consumer[i]] = 1'b0;
                        controller_state_next[i] = IDLE;
                    end
                end
            endcase
        end
    end

    // --- Sequential Logic (Registers) ---
    always_ff @(posedge clk) begin
        if (reset) begin
            controller_state <= '{default: IDLE};
            current_consumer <= '{default: '0};
            channel_serving_consumer <= '0;
        end else begin
            controller_state <= controller_state_next;
            current_consumer <= current_consumer_next;
            channel_serving_consumer <= channel_serving_consumer_next;
        end
    end

endmodule