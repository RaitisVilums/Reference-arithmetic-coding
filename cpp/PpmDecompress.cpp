/* 
 * Decompression application using prediction by partial matching (PPM) with arithmetic coding
 * 
 * Usage: PpmDecompress InputFile OutputFile
 * This decompresses files generated by the "PpmCompress" application.
 * 
 * Copyright (c) Project Nayuki
 * 
 * https://www.nayuki.io/page/reference-arithmetic-coding
 * https://github.com/nayuki/Reference-arithmetic-coding
 */

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
#include "ArithmeticCoder.hpp"
#include "BitIoStream.hpp"
#include "PpmModel.hpp"

using std::uint32_t;
using std::vector;


// Must be at least -1 and match PpmDecompress. Warning: Exponential memory usage at O(257^n).
static constexpr int MODEL_ORDER = 3;


static void decompress(BitInputStream &in, std::ostream &out);
static uint32_t decodeSymbol(ArithmeticDecoder &dec, PpmModel &model, const vector<uint32_t> &history);


int main(int argc, char *argv[]) {
	// Handle command line arguments
	if (argc != 3) {
		std::cerr << "Usage: " << argv[0] << " InputFile OutputFile" << std::endl;
		return EXIT_FAILURE;
	}
	const char *inputFile  = argv[1];
	const char *outputFile = argv[2];
	
	// Perform file decompression
	std::ifstream in(inputFile, std::ios::binary);
	std::ofstream out(outputFile, std::ios::binary);
	BitInputStream bin(in);
	try {
		decompress(bin, out);
		return EXIT_SUCCESS;
	} catch (const char *msg) {
		std::cerr << msg << std::endl;
		return EXIT_FAILURE;
	}
}


static void decompress(BitInputStream &in, std::ostream &out) {
	// Set up decoder and model. In this PPM model, symbol 256 represents EOF;
	// its frequency is 1 in the order -1 context but its frequency
	// is 0 in all other contexts (which have non-negative order).
	ArithmeticDecoder dec(32, in);
	PpmModel model(MODEL_ORDER, 257, 256);
	vector<uint32_t> history;
	
	while (true) {
		// Decode and write one byte
		uint32_t symbol = decodeSymbol(dec, model, history);
		if (symbol == 256)  // EOF symbol
			break;
		int b = static_cast<int>(symbol);
		if (std::numeric_limits<char>::is_signed)
			b -= (b >> 7) << 8;
		out.put(static_cast<char>(b));
		model.incrementContexts(history, symbol);
		
		if (model.modelOrder >= 1) {
			// Prepend current symbol, dropping oldest symbol if necessary
			if (history.size() >= static_cast<unsigned int>(model.modelOrder))
				history.erase(history.end() - 1);
			history.insert(history.begin(), symbol);
		}
	}
}


static uint32_t decodeSymbol(ArithmeticDecoder &dec, PpmModel &model, const vector<uint32_t> &history) {
	// Try to use highest order context that exists based on the history suffix. When symbol 256
	// is consumed at a context at any non-negative order, it means "escape to the next lower order
	// with non-empty context". When symbol 256 is consumed at the order -1 context, it means "EOF".
	for (int order = static_cast<int>(history.size()); order >= 0; order--) {
		PpmModel::Context *ctx = model.rootContext.get();
		for (int i = 0; i < order; i++) {
			if (ctx->subcontexts.empty())
				throw std::logic_error("Assertion error");
			ctx = ctx->subcontexts.at(history.at(i)).get();
			if (ctx == nullptr)
				goto outerEnd;
		}
		{
			uint32_t symbol = dec.read(ctx->frequencies);
			if (symbol < 256)
				return symbol;
		}
		// Else we read the context escape symbol, so continue decrementing the order
		outerEnd:;
	}
	// Logic for order = -1
	return dec.read(model.orderMinus1Freqs);
}
