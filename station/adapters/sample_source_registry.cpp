#include "orbita_stand/sample_source.h"
#include "orbita_stand/config.h"

namespace orbita::stand {

void registerSampleSourceComponents(ComponentRuntime& runtime)
{
    runtime.registerKindFactory("sample_source",
        [](const ComponentProfile& definition) -> std::unique_ptr<IStationComponent> {
            return createSampleSource(definition.provider, definition.configuration);
        });
}

} // namespace orbita::stand
