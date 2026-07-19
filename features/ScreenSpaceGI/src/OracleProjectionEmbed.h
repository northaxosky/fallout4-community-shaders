#pragma once

#include <array>
#include <cmath>

namespace cs::ssgi::dev
{
	namespace detail
	{
		using Matrix4 = std::array<std::array<double, 4>, 4>;

		inline bool InvertMatrix(const Matrix4& a_input, Matrix4& a_output)
		{
			std::array<std::array<double, 8>, 4> augmented{};
			for (std::size_t row = 0; row < 4; ++row) {
				for (std::size_t column = 0; column < 4; ++column) {
					augmented[row][column] = a_input[row][column];
					augmented[row][column + 4] = row == column ? 1.0 : 0.0;
				}
			}

			for (std::size_t column = 0; column < 4; ++column) {
				std::size_t pivotRow = column;
				double pivotMagnitude = std::fabs(augmented[pivotRow][column]);
				for (std::size_t row = column + 1; row < 4; ++row) {
					const double candidateMagnitude = std::fabs(augmented[row][column]);
					if (candidateMagnitude > pivotMagnitude) {
						pivotMagnitude = candidateMagnitude;
						pivotRow = row;
					}
				}

				if (pivotMagnitude == 0.0) {
					return false;
				}
				if (pivotRow != column) {
					const auto temporary = augmented[column];
					augmented[column] = augmented[pivotRow];
					augmented[pivotRow] = temporary;
				}

				const double pivot = augmented[column][column];
				for (std::size_t entry = 0; entry < 8; ++entry) {
					augmented[column][entry] /= pivot;
				}
				for (std::size_t row = 0; row < 4; ++row) {
					if (row == column) {
						continue;
					}
					const double factor = augmented[row][column];
					for (std::size_t entry = 0; entry < 8; ++entry) {
						augmented[row][entry] -= factor * augmented[column][entry];
					}
				}
			}

			for (std::size_t row = 0; row < 4; ++row) {
				for (std::size_t column = 0; column < 4; ++column) {
					a_output[row][column] = augmented[row][column + 4];
				}
			}
			return true;
		}
	}

	// Ports decode_reconstruct_test.py:350-354.
	inline void EmbedProjectionFromWorldToCam(
		const double a_worldToCam[16],
		const double a_rotationRows[9],
		const double a_cameraWorld[3],
		double a_output[16])
	{
		constexpr std::array<std::array<double, 3>, 3> swapXZ{ {
			{ 0.0, 0.0, 1.0 },
			{ 0.0, 1.0, 0.0 },
			{ 1.0, 0.0, 0.0 }
		} };
		std::array<std::array<double, 3>, 3> swappedRotation{};
		for (std::size_t row = 0; row < 3; ++row) {
			for (std::size_t column = 0; column < 3; ++column) {
				for (std::size_t inner = 0; inner < 3; ++inner) {
					swappedRotation[row][column] +=
						swapXZ[row][inner] * a_rotationRows[inner * 3 + column];
				}
			}
		}

		detail::Matrix4 nodeWorldToView{};
		for (std::size_t row = 0; row < 3; ++row) {
			for (std::size_t column = 0; column < 3; ++column) {
				nodeWorldToView[row][column] = swappedRotation[row][column];
			}
			for (std::size_t column = 0; column < 3; ++column) {
				nodeWorldToView[row][3] -= swappedRotation[row][column] * a_cameraWorld[column];
			}
		}
		nodeWorldToView[3][3] = 1.0;

		detail::Matrix4 inverseNode{};
		if (!detail::InvertMatrix(nodeWorldToView, inverseNode)) {
			for (std::size_t index = 0; index < 16; ++index) {
				a_output[index] = std::nan("");
			}
			return;
		}

		detail::Matrix4 worldToCam{};
		for (std::size_t row = 0; row < 4; ++row) {
			for (std::size_t column = 0; column < 4; ++column) {
				worldToCam[row][column] = a_worldToCam[row * 4 + column];
			}
		}

		detail::Matrix4 sceneProjection{};
		for (std::size_t row = 0; row < 4; ++row) {
			for (std::size_t column = 0; column < 4; ++column) {
				for (std::size_t inner = 0; inner < 4; ++inner) {
					sceneProjection[row][column] += worldToCam[row][inner] * inverseNode[inner][column];
				}
			}
		}

		for (std::size_t row = 0; row < 4; ++row) {
			for (std::size_t column = 0; column < 4; ++column) {
				a_output[row * 4 + column] = sceneProjection[column][row];
			}
		}
	}
}
