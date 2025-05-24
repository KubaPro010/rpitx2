#include <unistd.h>
#include <librpitx/librpitx.h>
#include <unistd.h>
#include "stdio.h"
#include <cstring>
#include <signal.h>
#include <stdlib.h>
#include <vector>
#include <cmath>

bool running = true;

// LoRa parameters
struct LoRaConfig {
    int spreading_factor;    // SF7-SF12
    float bandwidth;         // 125kHz, 250kHz, 500kHz
    int code_rate;          // 1-4 (4/5, 4/6, 4/7, 4/8)
    bool explicit_header;   // true for explicit header mode
    int preamble_length;    // number of preamble symbols
};

class LoRaModulator {
private:
    LoRaConfig config;
    int symbols_per_second;
    float symbol_time;
    int samples_per_symbol;
    int sample_rate;
    
public:
    LoRaModulator(LoRaConfig cfg, int sr) : config(cfg), sample_rate(sr) {
        symbols_per_second = static_cast<float>(config.bandwidth) / (1 << config.spreading_factor);
        symbol_time = 1.0f / symbols_per_second;
        samples_per_symbol = static_cast<int>(sample_rate * symbol_time);
    }
    
    // Convert binary string to symbols
    std::vector<int> dataToSymbols(const std::string& binary_data) {
        std::vector<int> symbols;
        
        // Add preamble (upchirps)
        for(int i = 0; i < config.preamble_length; i++) {
            symbols.push_back(0); // 0 represents base upchirp
        }
        
        // Add sync word (network ID) - using 0x34 as default
        symbols.push_back(52); // 0x34 mapped to symbol space
        symbols.push_back(52);
        
        // Process data in chunks based on spreading factor
        int bits_per_symbol = config.spreading_factor;
        
        for(size_t i = 0; i < binary_data.length(); i += bits_per_symbol) {
            std::string chunk = binary_data.substr(i, bits_per_symbol);
            
            // Pad if necessary
            while(chunk.length() < bits_per_symbol) {
                chunk += "0";
            }
            
            // Convert binary chunk to symbol value
            int symbol_value = 0;
            for(size_t j = 0; j < chunk.length(); j++) {
                if(chunk[j] == '1') {
                    symbol_value |= (1 << (bits_per_symbol - 1 - j));
                }
            }
            
            symbols.push_back(symbol_value);
        }
        
        return symbols;
    }
    
    // Generate chirp for a symbol
    void generateChirp(std::vector<float>& frequency_samples, int symbol_value, bool is_upchirp = true) {
        int num_samples = samples_per_symbol;
        float bandwidth_half = config.bandwidth / 2.0;
        
        for(int i = 0; i < num_samples; i++) {
            float t = (float)i / sample_rate;
            float freq_offset = 0;
            
            if(is_upchirp) {
                // Upchirp: frequency increases linearly
                float progress = (float)i / num_samples;
                freq_offset = -bandwidth_half + (config.bandwidth * progress);
                
                // Apply symbol offset
                freq_offset += (symbol_value * config.bandwidth) / (1 << config.spreading_factor);
                
                // Wrap frequency if it exceeds bandwidth
                freq_offset = fmod(freq_offset + bandwidth_half, config.bandwidth) - bandwidth_half;
            } else {
                // Downchirp: frequency decreases linearly
                float progress = (float)i / num_samples;
                freq_offset = bandwidth_half - (config.bandwidth * progress);
            }
            
            frequency_samples.push_back(freq_offset);
        }
    }
};

static void terminate(int num) {
    running = false;
    fprintf(stderr, "Caught signal - Terminating\n");
}

// Convert hex string to binary string
std::string hexToBinary(const std::string& hex) {
    std::string binary = "";
    for(char c : hex) {
        switch(c) {
            case '0': binary += "0000"; break;
            case '1': binary += "0001"; break;
            case '2': binary += "0010"; break;
            case '3': binary += "0011"; break;
            case '4': binary += "0100"; break;
            case '5': binary += "0101"; break;
            case '6': binary += "0110"; break;
            case '7': binary += "0111"; break;
            case '8': binary += "1000"; break;
            case '9': binary += "1001"; break;
            case 'A': case 'a': binary += "1010"; break;
            case 'B': case 'b': binary += "1011"; break;
            case 'C': case 'c': binary += "1100"; break;
            case 'D': case 'd': binary += "1101"; break;
            case 'E': case 'e': binary += "1110"; break;
            case 'F': case 'f': binary += "1111"; break;
            default: break;
        }
    }
    return binary;
}

int main(int argc, char* argv[]) {
    if(argc < 6) {
        printf("Usage: %s <frequency_hz> <spreading_factor> <bandwidth_hz> <data_format> <data>\n", argv[0]);
        printf("  frequency_hz: Center frequency in Hz\n");
        printf("  spreading_factor: 7-12\n");
        printf("  bandwidth_hz: 125000, 250000, or 500000\n");
        printf("  data_format: 'binary' or 'hex'\n");
        printf("  data: Binary string (e.g., '101010') or hex string (e.g., 'DEADBEEF')\n");
        printf("Example: %s 434000000 7 125000 hex DEADBEEF\n", argv[0]);
        exit(0);
    }
    
    float center_frequency = atof(argv[1]);
    int spreading_factor = atoi(argv[2]);
    float bandwidth = atof(argv[3]);
    std::string data_format = argv[4];
    std::string input_data = argv[5];
    
    // Validate parameters
    if(spreading_factor < 7 || spreading_factor > 12) {
        fprintf(stderr, "Error: Spreading factor must be between 7 and 12\n");
        exit(1);
    }
    
    if(bandwidth != 125000 && bandwidth != 250000 && bandwidth != 500000) {
        fprintf(stderr, "Error: Bandwidth must be 125000, 250000, or 500000 Hz\n");
        exit(1);
    }
    
    // Convert input data to binary
    std::string binary_data;
    if(data_format == "binary") {
        binary_data = input_data;
    } else if(data_format == "hex") {
        binary_data = hexToBinary(input_data);
    } else {
        fprintf(stderr, "Error: Data format must be 'binary' or 'hex'\n");
        exit(1);
    }
    
    printf("Transmitting binary data: %s\n", binary_data.c_str());
    printf("Data length: %zu bits\n", binary_data.length());
    
    // Set up signal handlers
    for(int i = 0; i < 64; i++) {
        struct sigaction sa;
        std::memset(&sa, 0, sizeof(sa));
        sa.sa_handler = terminate;
        sigaction(i, &sa, NULL);
    }
    
    // Configure LoRa parameters
    LoRaConfig lora_config;
    lora_config.spreading_factor = spreading_factor;
    lora_config.bandwidth = bandwidth;
    lora_config.code_rate = 1; // 4/5
    lora_config.explicit_header = true;
    lora_config.preamble_length = 8;
    
    int sample_rate = 1000000; // 1 MHz sample rate
    int fifo_size = 4096;
    
    // Initialize RF transmitter
    ngfmdmasync rf_transmitter(center_frequency, sample_rate, 14, fifo_size);
    
    // Create LoRa modulator
    LoRaModulator modulator(lora_config, sample_rate);
    
    // Convert data to symbols
    std::vector<int> symbols = modulator.dataToSymbols(binary_data);
    printf("Generated %zu symbols\n", symbols.size());
    
    // Generate frequency samples for all symbols
    std::vector<float> all_frequency_samples;
    
    for(size_t i = 0; i < symbols.size(); i++) {
        if(i < lora_config.preamble_length) {
            // Preamble upchirps
            modulator.generateChirp(all_frequency_samples, 0, true);
        } else if(i < lora_config.preamble_length + 2) {
            // Sync word
            modulator.generateChirp(all_frequency_samples, symbols[i], true);
        } else {
            // Data symbols
            modulator.generateChirp(all_frequency_samples, symbols[i], true);
        }
    }
    
    printf("Generated %zu frequency samples\n", all_frequency_samples.size());
    printf("Starting transmission...\n");
    
    // Transmit the data
    size_t sample_index = 0;
    bool transmission_complete = false;
    
    while(running && !transmission_complete) {
        usleep(fifo_size * 1000000.0 * 3.0 / (4.0 * sample_rate));
        
        int available = rf_transmitter.GetBufferAvailable();
        if(available > fifo_size / 2) {
            int buffer_index = rf_transmitter.GetUserMemIndex();
            
            for(int j = 0; j < available && sample_index < all_frequency_samples.size(); j++) {
                rf_transmitter.SetFrequencySample(buffer_index + j, all_frequency_samples[sample_index]);
                sample_index++;
            }
            
            if(sample_index >= all_frequency_samples.size()) {
                transmission_complete = true;
                printf("Transmission complete!\n");
            }
        }
        
        if(sample_index < all_frequency_samples.size()) {
            float progress = (float)sample_index / all_frequency_samples.size() * 100.0;
            fprintf(stderr, "Progress: %.1f%%\r", progress);
        }
    }
    
    fprintf(stderr, "\nEnd\n");
    rf_transmitter.stop();
    
    return 0;
}