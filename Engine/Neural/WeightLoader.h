// WeightLoader.h
// Neural network weight loading from Stockfish NNUE format
//
// This module handles loading pre-trained weights from Stockfish .nnue files.
// Supports LEB128 compression used in modern Stockfish networks.
//
// File format overview:
// - Version header (uint32)
// - Architecture hash (uint32)
// - Feature transformer weights (LEB128 compressed)
// - Network layer weights (little-endian)
//
// Note: Only small network format is currently supported

#pragma once

#include "Transformer.h"
#include "DenseLayer.h"
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>
#include <memory>

namespace Chess
{
namespace Neural
{
    // NNUE file format constants
    constexpr uint32_t NNUE_VERSION = 0x7AF32F20u;
    constexpr char LEB128_MAGIC[] = "COMPRESSED_LEB128";
    constexpr int LEB128_MAGIC_SIZE = 17;

    // Network architecture hash for validation
    // This should match the architecture being loaded
    constexpr uint32_t SMALL_NET_HASH = 0x3E5AA6EEu;

    // ========== LEB128 DECODER ==========

    // Little-Endian Base-128 variable-length decoder
    // Used for compressed weight storage in .nnue files
    class Leb128Decoder
    {
    public:
        Leb128Decoder() = default;

        // Initialize decoder with compressed data
        // @param data: Pointer to compressed byte stream
        // @param size: Size of compressed data in bytes
        void Initialize(const uint8_t* data, size_t size)
        {
            m_data = data;
            m_size = size;
            m_pos = 0;
        }

        // Check if decoder has more data
        bool HasData() const { return m_pos < m_size; }

        // Decode single signed integer (variable length)
        // @return: Decoded int32 value
        int32_t DecodeInt32()
        {
            uint32_t result = 0;
            int shift = 0;
            uint8_t byte;

            do
            {
                if (m_pos >= m_size)
                    return 0;

                byte = m_data[m_pos++];
                result |= (static_cast<uint32_t>(byte & 0x7F) << shift);
                shift += 7;
            } while (byte & 0x80);

            // Sign extension for negative numbers
            if (shift < 32 && (byte & 0x40))
            {
                result |= (~0u << shift);
            }

            return static_cast<int32_t>(result);
        }

        // Decode int16 (truncated from int32)
        int16_t DecodeInt16()
        {
            return static_cast<int16_t>(DecodeInt32());
        }

        // Get current position in stream
        size_t GetPosition() const { return m_pos; }

    private:
        const uint8_t* m_data = nullptr;
        size_t m_size = 0;
        size_t m_pos = 0;
    };

    // ========== WEIGHT LOADER CLASS ==========

    // Loads neural network weights from Stockfish .nnue files
    class WeightLoader
    {
    public:
        // Load result status
        enum class LoadResult
        {
            Success,
            FileNotFound,
            InvalidFormat,
            VersionMismatch,
            ArchitectureMismatch,
            ReadError,
            CompressionError
        };

        WeightLoader() = default;

        // Load weights from .nnue file into network components
        // @param filename: Path to .nnue file
        // @param transformer: Feature transformer to populate
        // @param layer1: First hidden layer
        // @param layer2: Second hidden layer
        // @param outputLayer: Final output layer
        // @return: Load result status
        template<int TransformerDim, int Layer1In, int Layer1Out,
                 int Layer2In, int Layer2Out, int OutputIn>
        LoadResult LoadNetwork(const std::string& filename,
                                Transformer<TransformerDim>& transformer,
                                DenseLayer<Layer1In, Layer1Out>& layer1,
                                DenseLayer<Layer2In, Layer2Out>& layer2,
                                OutputLayer<OutputIn>& outputLayer)
        {
            std::ifstream file(filename, std::ios::binary);
            if (!file.is_open())
                return LoadResult::FileNotFound;

            // Read entire file into memory for faster processing
            file.seekg(0, std::ios::end);
            size_t fileSize = file.tellg();
            file.seekg(0, std::ios::beg);

            // Sanity check file size to catch corrupted or wrong files early
            // Small network is approximately 5.2 MB
            constexpr size_t MIN_FILE_SIZE = 1024;               // 1 KB minimum
            constexpr size_t MAX_FILE_SIZE = 50 * 1024 * 1024;   // 50 MB maximum

            if (fileSize < MIN_FILE_SIZE || fileSize > MAX_FILE_SIZE)
                return LoadResult::InvalidFormat;

            m_buffer.resize(fileSize);
            if (!file.read(reinterpret_cast<char*>(m_buffer.data()), fileSize))
                return LoadResult::ReadError;

            m_pos = 0;

            // Verify file header
            uint32_t version = ReadUint32();
            if (version != NNUE_VERSION)
                return LoadResult::VersionMismatch;

            // Validate architecture hash matches our network structure
            // This prevents loading incompatible networks that would cause crashes
            uint32_t archHash = ReadUint32();
            if (archHash != SMALL_NET_HASH)
            {
                // Allow hash of zero for legacy compatibility
                if (archHash != 0)
                    return LoadResult::ArchitectureMismatch;
            }

            // Load feature transformer
            LoadResult result = LoadTransformer(transformer);
            if (result != LoadResult::Success)
                return result;

            // Load network layers
            result = LoadDenseLayer(layer1);
            if (result != LoadResult::Success)
                return result;

            result = LoadDenseLayer(layer2);
            if (result != LoadResult::Success)
                return result;

            result = LoadOutputLayer(outputLayer);
            if (result != LoadResult::Success)
                return result;

            return LoadResult::Success;
        }

        // Simplified loader that initializes a complete small network
        // @param filename: Path to .nnue file
        // @return: Load result status
        LoadResult LoadSmallNetwork(const std::string& filename,
                                     SmallTransformer& transformer,
                                     DenseLayer<256, 16>& layer1,
                                     DenseLayer<32, 32>& layer2,
                                     OutputLayer<32>& output)
        {
            return LoadNetwork(filename, transformer, layer1, layer2, output);
        }

        // Get human-readable error message
        static const char* GetErrorMessage(LoadResult result)
        {
            switch (result)
            {
            case LoadResult::Success:
                return "Success";
            case LoadResult::FileNotFound:
                return "NNUE file not found";
            case LoadResult::InvalidFormat:
                return "Invalid NNUE file format";
            case LoadResult::VersionMismatch:
                return "NNUE version mismatch";
            case LoadResult::ArchitectureMismatch:
                return "Network architecture mismatch";
            case LoadResult::ReadError:
                return "Error reading NNUE file";
            case LoadResult::CompressionError:
                return "Decompression error in NNUE file";
            default:
                return "Unknown error";
            }
        }

    private:
        // Read little-endian uint32 from buffer
        uint32_t ReadUint32()
        {
            if (m_pos + 4 > m_buffer.size())
                return 0;

            uint32_t value = m_buffer[m_pos] |
                            (m_buffer[m_pos + 1] << 8) |
                            (m_buffer[m_pos + 2] << 16) |
                            (m_buffer[m_pos + 3] << 24);
            m_pos += 4;
            return value;
        }

        // Read little-endian int32 from buffer
        int32_t ReadInt32()
        {
            return static_cast<int32_t>(ReadUint32());
        }

        // Read single int8 from buffer
        int8_t ReadInt8()
        {
            if (m_pos >= m_buffer.size())
                return 0;
            return static_cast<int8_t>(m_buffer[m_pos++]);
        }

        // Read int16 little-endian from buffer
        int16_t ReadInt16()
        {
            if (m_pos + 2 > m_buffer.size())
                return 0;

            int16_t value = m_buffer[m_pos] | (m_buffer[m_pos + 1] << 8);
            m_pos += 2;
            return value;
        }

        // Check for LEB128 compression magic
        bool CheckLeb128Magic()
        {
            if (m_pos + LEB128_MAGIC_SIZE > m_buffer.size())
                return false;

            for (int i = 0; i < LEB128_MAGIC_SIZE; ++i)
            {
                if (m_buffer[m_pos + i] != static_cast<uint8_t>(LEB128_MAGIC[i]))
                    return false;
            }
            m_pos += LEB128_MAGIC_SIZE;
            return true;
        }

        // Load feature transformer weights
        template<int Dim>
        LoadResult LoadTransformer(Transformer<Dim>& transformer)
        {
            // Skip header hash
            ReadUint32();

            // Check for LEB128 compression
            bool compressed = CheckLeb128Magic();

            if (compressed)
            {
                // Read compressed size
                size_t compressedSize = ReadUint32();
                if (m_pos + compressedSize > m_buffer.size())
                    return LoadResult::ReadError;

                Leb128Decoder decoder;
                decoder.Initialize(&m_buffer[m_pos], compressedSize);

                // Load biases
                int16_t* biases = transformer.GetBiases();
                for (int i = 0; i < Dim; ++i)
                {
                    biases[i] = decoder.DecodeInt16();
                }

                // Load weights
                int16_t* weights = transformer.GetWeights();
                int weightCount = Transformer<Dim>::GetInputDim() *
                                  Transformer<Dim>::GetPaddedOutputDim();
                for (int i = 0; i < weightCount; ++i)
                {
                    weights[i] = decoder.DecodeInt16();
                }

                // Load PSQT weights
                int32_t* psqt = transformer.GetPsqtWeights();
                int psqtCount = Transformer<Dim>::GetInputDim() * PSQT_BUCKETS;
                for (int i = 0; i < psqtCount; ++i)
                {
                    psqt[i] = decoder.DecodeInt32();
                }

                m_pos += compressedSize;
            }
            else
            {
                // Uncompressed format - read directly
                int16_t* biases = transformer.GetBiases();
                for (int i = 0; i < Dim; ++i)
                {
                    biases[i] = ReadInt16();
                }

                int16_t* weights = transformer.GetWeights();
                int weightCount = Transformer<Dim>::GetInputDim() *
                                  Transformer<Dim>::GetPaddedOutputDim();
                for (int i = 0; i < weightCount; ++i)
                {
                    weights[i] = ReadInt16();
                }

                int32_t* psqt = transformer.GetPsqtWeights();
                int psqtCount = Transformer<Dim>::GetInputDim() * PSQT_BUCKETS;
                for (int i = 0; i < psqtCount; ++i)
                {
                    psqt[i] = ReadInt32();
                }
            }

            return LoadResult::Success;
        }

        // Load dense layer weights
        template<int InDim, int OutDim>
        LoadResult LoadDenseLayer(DenseLayer<InDim, OutDim>& layer)
        {
            // Skip layer hash
            ReadUint32();

            // Load biases (int32)
            int32_t* biases = layer.GetBiases();
            for (int i = 0; i < OutDim; ++i)
            {
                biases[i] = ReadInt32();
            }

            // Load weights (int8)
            int8_t* weights = layer.GetWeights();
            int paddedIn = DenseLayer<InDim, OutDim>::PADDED_INPUT;
            for (int o = 0; o < OutDim; ++o)
            {
                for (int i = 0; i < paddedIn; ++i)
                {
                    weights[o * paddedIn + i] = (i < InDim) ? ReadInt8() : 0;
                }
            }

            return LoadResult::Success;
        }

        // Load output layer weights
        template<int InDim>
        LoadResult LoadOutputLayer(OutputLayer<InDim>& layer)
        {
            // Skip layer hash
            ReadUint32();

            // Load bias (single int32)
            layer.GetBias() = ReadInt32();

            // Load weights (int8)
            int8_t* weights = layer.GetWeights();
            int paddedIn = OutputLayer<InDim>::PADDED_INPUT;
            for (int i = 0; i < paddedIn; ++i)
            {
                weights[i] = (i < InDim) ? ReadInt8() : 0;
            }

            return LoadResult::Success;
        }

        std::vector<uint8_t> m_buffer;
        size_t m_pos = 0;
    };

} // namespace Neural
} // namespace Chess