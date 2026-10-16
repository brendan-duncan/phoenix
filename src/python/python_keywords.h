#pragma once

#include <string>
#include <vector>

namespace python {

/// The names of a method's parameters, in order, so Python scripts can pass
/// them as keywords: timeline.insert_frames(5, all_layers=True). \a owner is
/// the class that defines the method ("Timeline"). Null for a method with
/// none listed, which takes positions only.
///
/// The names are the JSFL reference's, without its b and str prefixes
/// (fileURI, currentPNGSettings); a keyword may be the name or its
/// snake_case form (file_uri, current_png_settings).
const std::vector<std::string>* parameterNames(const std::string& owner, const std::string& method);

} // namespace python
