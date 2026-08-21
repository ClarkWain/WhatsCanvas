#pragma once

#include <cstddef>
#include <memory>

#include "Export.h"

namespace wsc {

class Canvas;

/// Immutable, backend-neutral sequence of Canvas operations.
///
/// A Picture owns CPU-side drawing inputs only. It deliberately does not own
/// backend command buffers, textures, or other GPU-context objects, so the same
/// Picture can be replayed after a context is recreated or on another Canvas.
/// The callback passed to Canvas::recordPicture is synchronous. Operations that
/// depend on destination/backend state, including saveLayer and DPR changes,
/// reject recording and make recordPicture return an empty shared pointer.
class WSC_API Picture final
{
public:
	~Picture();

	Picture(const Picture &) = delete;

	Picture &operator=(const Picture &) = delete;

	Picture(Picture &&) = delete;

	Picture &operator=(Picture &&) = delete;

	/// Number of retained Canvas operations.
	std::size_t operationCount() const;

	bool empty() const { return operationCount() == 0; }

private:
	friend class Canvas;
	struct Impl;
	explicit Picture(std::unique_ptr<Impl> impl);

	std::unique_ptr<Impl> impl_;
};

} // namespace wsc
