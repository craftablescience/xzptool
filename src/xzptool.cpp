// ReSharper disable CppParameterMayBeConst
// ReSharper disable CppRedundantParentheses
// ReSharper disable CppRedundantQualifier

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

constexpr uint32_t XZ_SIGNATURE_COMPLEX = ('x' << 24) | ('C' << 16) | ('m' << 8) | ('p' << 0);
constexpr uint32_t XZ_SIGNATURE_SIMPLE  = ('x' << 24) | ('S' << 16) | ('m' << 8) | ('p' << 0);

bool g_verbose = false;

#define VERBOSE_LOG(...) do { if ( g_verbose) { std::cout << __VA_ARGS__; } } while (0)
#define NVRBOSE_LOG(...) do { if (!g_verbose) { std::cout << __VA_ARGS__; } } while (0)
#define REGULAR_LOG(...) do {                   std::cout << __VA_ARGS__;   } while (0)

void* __stdcall JC_alloc(uint32_t size)         { return std::malloc(size); }
bool  __stdcall JC_free(void* mem)              { std::free(mem); return true; }

// Taken from sourcepp::math
[[nodiscard]] constexpr uint64_t paddingForAlignment(uint64_t alignment, uint64_t n) {
	if (const auto rest = n % alignment; rest > 0) {
		return alignment - rest;
	}
	return 0;
}

void compress(const std::string& inputPath, const std::string& outputPath) {
	FileStream reader{inputPath, FileStream::OPT_READ};
	FileStream writer{outputPath, FileStream::OPT_TRUNCATE | FileStream::OPT_CREATE_IF_NONEXISTENT};

	const auto decompressedSize = std::filesystem::file_size(inputPath);
	REGULAR_LOG("Decompressed size: " << decompressedSize << " bytes" << std::endl);

	constexpr uint32_t READ_BLOCK_SIZE = 1024 * 512; // 0.5mb
	REGULAR_LOG("Read block size: " << READ_BLOCK_SIZE << " bytes" << std::endl);

	constexpr uint32_t WINDOW_SIZE = 1024 * 16; // 16kb
	REGULAR_LOG("Window size: " << WINDOW_SIZE << " bytes" << std::endl);

	writer
		.write<uint32_t>(XZ_SIGNATURE_COMPLEX)
		.write<uint32_t>(1)
		.write<uint32_t>(decompressedSize)
		.write(READ_BLOCK_SIZE);
	const auto decompressionBufferSizePos = writer.tell_out();
	writer
		.write<uint32_t>(0)
		.write(WINDOW_SIZE);

	uint32_t compressedBlockSize = 0, decompressedBlockSize = 0, maxDecompressedBlockSize = 0;

	bool looping = true;
	while (looping) {
		std::vector<std::byte> decompressedBuffer;
		if (reader.tell_in() + WINDOW_SIZE < decompressedSize) {
			decompressedBuffer = reader.read_bytes(WINDOW_SIZE);
		} else {
			decompressedBuffer = reader.read_bytes(decompressedSize - reader.tell_in());
			looping = false;
		}

		std::vector<std::byte> compressedBuffer(sizeof(uint16_t) + WINDOW_SIZE);
		if (
			const auto compressedSize = JCALG1_Compress(decompressedBuffer.data(), decompressedBuffer.size(), compressedBuffer.data() + sizeof(uint16_t), WINDOW_SIZE, &::JC_alloc, &::JC_free, nullptr, true);
			!compressedSize || compressedSize > WINDOW_SIZE
		) {
			// Failed to compress or compressed size is greater than decompressed size
			BufferStream compressedStream{compressedBuffer};
			compressedStream
				.write<uint16_t>(0x8000 | static_cast<uint16_t>(decompressedBuffer.size()))
				.write(decompressedBuffer);
			compressedBuffer.resize(compressedStream.size());
			VERBOSE_LOG("Creating uncompressed block with size " << decompressedBuffer.size() << " bytes" << std::endl);
		} else {
			compressedBuffer.resize(sizeof(uint16_t) + compressedSize);
			BufferStream{compressedBuffer}.write<uint16_t>(compressedBuffer.size() - sizeof(uint16_t));
			VERBOSE_LOG("Creating compressed block with size " << compressedSize << " bytes" << std::endl);
		}

		if (compressedBlockSize + compressedBuffer.size() > READ_BLOCK_SIZE) {
			VERBOSE_LOG("Creating terminating block with size 0 bytes" << std::endl);
			writer.write<uint16_t>(0).pad(::paddingForAlignment(READ_BLOCK_SIZE, writer.tell_out()));
			maxDecompressedBlockSize = std::max(decompressedBlockSize, maxDecompressedBlockSize);

			compressedBlockSize = 0;
			decompressedBlockSize = 0;
		}

		writer.write(compressedBuffer);
		compressedBlockSize += compressedBuffer.size();
		decompressedBlockSize += decompressedBuffer.size();
	}
	VERBOSE_LOG("Creating terminating block with size 0 bytes" << std::endl);
	writer.write<uint16_t>(0).pad(::paddingForAlignment(2048, writer.tell_out()));

	maxDecompressedBlockSize = std::max(decompressedBlockSize, maxDecompressedBlockSize) + 128;
	writer.seek_out_u(decompressionBufferSizePos).write(maxDecompressedBlockSize);
	REGULAR_LOG("Decompression buffer size: " << maxDecompressedBlockSize << " bytes" << std::endl);
}

void decompress(const std::string& inputPath, const std::string& outputPath) {
	FileStream reader{inputPath, FileStream::OPT_READ};
	FileStream writer{outputPath, FileStream::OPT_TRUNCATE | FileStream::OPT_CREATE_IF_NONEXISTENT};

	if (const auto signature = reader.read<uint32_t>(); signature == XZ_SIGNATURE_SIMPLE) {
		// Quick and easy
		const auto decompressedSize = reader.read<uint32_t>();
		REGULAR_LOG("Decompressed size: " << decompressedSize << " bytes" << std::endl);
		const auto compressedBlock = reader.read_bytes(std::filesystem::file_size(inputPath) - (sizeof(uint32_t) * 2));
		std::vector<std::byte> decompressedBlock(decompressedSize);
		const auto decompressedBlockSize = JCALG1_Decompress_Fast(compressedBlock.data(), decompressedBlock.data());
		if (decompressedBlockSize > decompressedSize) {
			throw std::runtime_error{"Found decompressed block size (" + std::to_string(decompressedBlockSize) + ") which is greater than the size given in the simple header, tell a programmer!"};
		}
		VERBOSE_LOG(" ...decompressed block with size " << decompressedBlockSize << " bytes" << std::endl);
		writer.write(decompressedBlock.data(), decompressedBlockSize);
	} else if (signature == XZ_SIGNATURE_COMPLEX) {
		// Not quick and easy
		if (reader.read<uint32_t>() != 1) {
			throw std::runtime_error{"Input file has an unsupported version!"};
		}

		const auto decompressedSize = reader.read<uint32_t>();
		REGULAR_LOG("Decompressed size: " << decompressedSize << " bytes" << std::endl);
		const auto readBlockSize = reader.read<uint32_t>();
		REGULAR_LOG("Read block size: " << readBlockSize << " bytes" << std::endl);
		const auto decompressionBufferSize = reader.read<uint32_t>();
		REGULAR_LOG("Decompression buffer size: " << decompressionBufferSize << " bytes" << std::endl);
		const auto windowSize = reader.read<uint32_t>();
		REGULAR_LOG("Window size: " << windowSize << " bytes" << std::endl);

		std::vector<std::byte> decompressedBlock(decompressionBufferSize);

		bool firstBlock = true;
		while (writer.tell_out() < decompressedSize) {
			const auto leftoverSize = decompressedSize - writer.tell_out();
			const auto block = reader.read_bytes(leftoverSize < readBlockSize ? leftoverSize : (readBlockSize - (firstBlock * sizeof(uint32_t) * 6)));
			BufferStreamReadOnly blockStream{block.data(), block.size()};
			if (blockStream.size() < readBlockSize) {
				VERBOSE_LOG("Next block has a size less than the read block size. This should only happen on the last block, but it should read OK either way." << std::endl);
			}

			NVRBOSE_LOG('\n' << std::unitbuf);
			while (blockStream.tell() < blockStream.size()) {
				auto compressedBlockSize = blockStream.read<uint16_t>();
				if (compressedBlockSize == 0) {
					VERBOSE_LOG("Sector clear" << std::endl);
					NVRBOSE_LOG("\r\033[A" << std::fixed << std::setprecision(2) << std::setw(6) << (static_cast<float>(writer.tell_out()) / static_cast<float>(decompressedSize) * 100.f) << '%');
					break;
				}
				if (compressedBlockSize & 0x8000) {
					compressedBlockSize &= ~0x8000;
					VERBOSE_LOG("Found uncompressed block with size " << compressedBlockSize << " bytes" << std::endl);
					writer.write(blockStream.read_bytes(compressedBlockSize));
				} else {
					VERBOSE_LOG("Found compressed block with size " << compressedBlockSize << " bytes" << std::endl);
					const auto compressedBlock = blockStream.read_bytes(compressedBlockSize);
					const auto decompressedBlockSize = JCALG1_Decompress_Fast(compressedBlock.data(), decompressedBlock.data());
					if (decompressedBlockSize > decompressionBufferSize) {
						throw std::runtime_error{"Found decompressed block size (" + std::to_string(decompressedBlockSize) + ") which is greater than maximum decompression buffer size, tell a programmer!"};
					}
					VERBOSE_LOG(" ...decompressed block with size " << decompressedBlockSize << " bytes" << std::endl);
					writer.write(decompressedBlock.data(), decompressedBlockSize);
				}
			}

			firstBlock = false;
		}
	} else {
		throw std::runtime_error{"Input file is not an xz_ file!"};
	}

	VERBOSE_LOG("Done" << std::endl);
	NVRBOSE_LOG(std::endl);
}

#undef VERBOSE_LOG
#undef NVRBOSE_LOG
#undef REGULAR_LOG

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

	cli.add_argument("-v", "--verbose").help("Print out more information.").flag().store_into(g_verbose);

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
			::compress(inputPath, outputPath);
		} else {
			::decompress(inputPath, outputPath);
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
