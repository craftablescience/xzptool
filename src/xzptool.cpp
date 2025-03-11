#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <argparse/argparse.hpp>
#include <FileStream.h>
#include <jcalg1.hpp>

#include "config.h"

using namespace std::string_view_literals;

#ifdef _WIN32
#define CP_UTF8 65001
extern "C" __declspec(dllimport) int __stdcall SetConsoleOutputCP(unsigned int);
#endif

namespace {

constexpr uint32_t XZ_SIGNATURE = ('x' << 24) | ('C' << 16) | ('m' << 8) | ('p');

#define VERBOSE_LOG(...) do { if (verbose) { std::cout << __VA_ARGS__ << std::endl; } } while (0)

void compress(const std::string& inputPath, const std::string& outputPath, bool verbose) {
	throw std::runtime_error{"Unimplemented right now :("};
}

void decompress(const std::string& inputPath, const std::string& outputPath, bool verbose) {
	FileStream reader{inputPath, FileStream::OPT_READ};
	FileStream writer{outputPath, FileStream::OPT_TRUNCATE | FileStream::OPT_CREATE_IF_NONEXISTENT};

	if (reader.read<uint32_t>() != XZ_SIGNATURE) {
		throw std::runtime_error{"Input file is not an xz_ file!"};
	}
	if (reader.read<uint32_t>() != 1) {
		throw std::runtime_error{"Input file has an unsupported version!"};
	}

	const auto decompressedSize = reader.read<uint32_t>();
	std::cout << "Decompressed size: " << decompressedSize << " bytes" << std::endl;
	const auto readBlockSize = reader.read<uint32_t>();
	std::cout << "Read block size: " << readBlockSize << " bytes" << std::endl;
	const auto decompressionBufferSize = reader.read<uint32_t>();
	std::cout << "Decompression buffer size: " << decompressionBufferSize << " bytes" << std::endl;
	const auto windowSize = reader.read<uint32_t>();
	std::cout << "Window size: " << windowSize << " bytes" << std::endl;

	std::vector<std::byte> decompressedBlock(windowSize);

	bool firstBlock = true;
	while (writer.tell_out() < decompressedSize) {
		const auto leftoverSize = decompressedSize - writer.tell_out();
		const auto block = reader.read_bytes(leftoverSize < readBlockSize ? leftoverSize : (readBlockSize - (firstBlock * sizeof(uint32_t) * 6)));
		BufferStreamReadOnly blockStream{block.data(), block.size()};
		if (blockStream.size() < readBlockSize) {
			VERBOSE_LOG("Next block has a size less than the read block size. This shouldn't happen, but it should read fine.");
		}

		if (!verbose) {
			std::cout << '\n' << std::unitbuf;
		}
		while (true) {
			auto compressedBlockSize = blockStream.read<uint16_t>();
			if (compressedBlockSize == 0) {
				VERBOSE_LOG("Sector clear");
				if (!verbose) {
					std::cout << "\r\033[A" << std::fixed << std::setprecision(2) << std::setw(6) << (static_cast<float>(writer.tell_out()) / static_cast<float>(decompressedSize) * 100.f) << '%';
				}
				break;
			}
			if (compressedBlockSize & 0x8000) {
				compressedBlockSize &= ~0x8000;
				VERBOSE_LOG("Found uncompressed block with size " << compressedBlockSize << " bytes");
				writer.write(blockStream.read_bytes(compressedBlockSize));
			} else {
				VERBOSE_LOG("Found compressed block with size " << compressedBlockSize << " bytes");
				const auto compressedBlock = blockStream.read_bytes(compressedBlockSize);
				const auto decompressedBlockSize = JCALG1_Decompress_Fast(compressedBlock.data(), decompressedBlock.data());
				if (decompressedBlockSize > windowSize) {
					throw std::runtime_error{"Found decompressed block size (" + std::to_string(decompressedBlockSize) + ") which is greater than window size! Tell a programmer!"};
				}
				VERBOSE_LOG(" ...decompressed block with size " << decompressedBlockSize << " bytes");
				writer.write(decompressedBlock.data(), decompressedBlockSize);
			}
		}

		firstBlock = false;
	}
	VERBOSE_LOG("Done");
	if (!verbose) {
		std::cout << std::endl;
	}
}

#undef VERBOSE_LOG

} // namespace

int main(int argc, const char* const argv[]) {
#ifdef _WIN32
	SetConsoleOutputCP(CP_UTF8); // Set up console to show UTF-8 characters
	setvbuf(stdout, nullptr, _IOFBF, 1000); // Enable buffering so VS terminal won't chop up UTF-8 byte sequences
#endif
	std::cout.precision(3);

	argparse::ArgumentParser cli{PROJECT_NAME, PROJECT_VERSION, argparse::default_arguments::help};

#ifdef _WIN32
	// Add the Windows-specific ones because why not
	cli.set_prefix_chars("-/");
	cli.set_assign_chars("=:");
#endif

	std::string inputPath;
	cli.add_argument("file").metavar("PATH").help("The path to the input file.").required().store_into(inputPath);

	std::string outputPath;
	cli.add_argument("-o", "--output").metavar("PATH").help("The path to the output file.").store_into(outputPath);

	bool verbose = false;
	cli.add_argument("-v", "--verbose").help("Print out more information.").flag().store_into(verbose);

	bool overwrite = false;
	cli.add_argument("-y").help("Automatically say yes to any prompts.").flag().store_into(overwrite);

	cli.add_epilog(
		PROJECT_NAME " — version v" PROJECT_VERSION " — created by " PROJECT_ORGANIZATION_NAME " — licensed under MIT\n"
		"Want to report a bug or request a feature? Make an issue at " PROJECT_HOMEPAGE_URL "/issues"
	);

	try {
		cli.parse_args(argc, argv);

		// Compressing or decompressing?
		bool isCompressing;
		if (inputPath.ends_with(".xz_")) {
			isCompressing = false;
		} else if (inputPath.ends_with(".xzp")) {
			isCompressing = true;
		} else {
			throw std::runtime_error{"Cannot work with a file that does not end in .xz_ or .xzp!"};
		}

		// Check input path is valid
		if (!std::filesystem::exists(inputPath)) {
			throw std::runtime_error{"Input path does not exist!"};
		}
		if (!std::filesystem::is_regular_file(inputPath)) {
			throw std::runtime_error{"Input path does not point to a file!"};
		}

		// Get output path, check it's valid
		if (outputPath.empty()) {
			const std::filesystem::path inputPathPath{inputPath};
			outputPath = (inputPathPath.parent_path() / inputPathPath.stem()).string() + (isCompressing ? ".xz_" : ".xzp");
		}
		if (const bool exists = std::filesystem::exists(outputPath); exists && !std::filesystem::is_regular_file(outputPath)) {
			throw std::runtime_error{"Output path must not be a directory!"};
		} else if (exists && !overwrite) {
			std::string in;
			while (in.empty() || (!in.starts_with('y') && !in.starts_with('Y') && !in.starts_with('n') && !in.starts_with('N'))) {
				std::cout << "Output file already exists. Overwrite? (y/N) ";
				std::cin >> in;
			}
			if (in.empty() || in.starts_with('n') || in.starts_with('N')) {
				std::cout << "Output file already exists. Aborting." << std::endl;
				return EXIT_SUCCESS;
			}
		} else if (exists) {
			std::cout << "Output file already exists, overwriting..." << std::endl;
		}

		// Do the thing
		if (isCompressing) {
			::compress(inputPath, outputPath, verbose);
		} else {
			::decompress(inputPath, outputPath, verbose);
		}
	} catch (const std::exception& e) {
		if (argc > 1) {
			std::cerr << e.what() << std::endl;
		} else {
			std::cout << cli << std::endl;
		}
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
