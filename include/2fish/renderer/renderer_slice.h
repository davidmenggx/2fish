#pragma once

template <typename T>
struct RendererSlice {
	T data_;
	bool is_loaded_{ false };
};
