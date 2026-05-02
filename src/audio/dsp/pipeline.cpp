#include "pipeline.h"

#include <set>
#include <sstream>

namespace lw::dsp {

std::string Pipeline::validate() const {
    std::set<FieldId> available {FieldId::Samples, FieldId::Format};
    for (size_t idx = 0; idx < stages_.size(); ++idx) {
        const auto& stage = *stages_[idx];
        for (FieldId f : stage.reads()) {
            if (available.find(f) == available.end()) {
                std::ostringstream os;
                os << "Pipeline composition error: stage " << idx
                   << " (" << stage.name() << ") reads field id "
                   << static_cast<int>(f)
                   << " not produced by any earlier stage";
                return os.str();
            }
        }
        for (FieldId f : stage.writes()) {
            available.insert(f);
        }
    }
    return {};
}

}  // namespace lw::dsp
