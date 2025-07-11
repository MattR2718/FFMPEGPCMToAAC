// FFMPEGPCMToAAC.cpp : Defines the entry point for the application.
//

#include "FFMPEGPCMToAAC.h"


int main(int argc, char** argv){

	std::string inputFile = "input.mp3"; // Default input audio file path
	std::string outputFile = "output.aac"; // Default output audio file path
	if (argc == 2) { inputFile = argv[1]; }
	else if (argc == 3) { inputFile = argv[1]; outputFile = argv[2]; }
	else {
		std::cout << "Usage: " << argv[0] << " <input_file> [output_file]\n";
		std::cout << "Default input file: input.mp3\n";
		std::cout << "Default output file: output.aac\n";
		std::cout << "Attempting to use defaults...\n";
	}

	try {
		std::vector<int16_t> pcm_data = decode_audio_to_pcm16(inputFile);
		std::cout << "Decoded " << pcm_data.size() << " samples at 16kHz mono 16-bit.\n";


		// 16kHz => 16k samples per second
		// Send 1s of audio to encoder at a time
		// Create encoder that writes to an aac file
		AudioEncoder encoder(outputFile);
		for (int i = 0; i < pcm_data.size(); i += 16000) {
			int num_samples = std::min(16000, static_cast<int>(pcm_data.size() - i));
			if (num_samples <= 0) {
				break; // No more samples to process
			}

			// Encode audio
			encoder.pushSamples(pcm_data.data() + i, num_samples);

		}


		// Wait until encoding thread fininshes
		// Stop the encoder and flush any remaining data
		encoder.stop();

	}
	catch (const std::exception& ex) {
		std::cerr << "Error: " << ex.what() << "\n";
		return 1;
	}

}
