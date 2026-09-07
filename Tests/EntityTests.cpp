#include <gtest/gtest.h>

#include "gisengine/core/entity.h"
#include "gisengine/core/guid.h"

namespace gisengine::tests {

TEST(EntityPoolTest, RejectsStaleHandleAfterIndexReuse) {
    core::EntityPool entities;
    const core::EntityHandle original = entities.create();

    ASSERT_TRUE(entities.destroy(original));
    const core::EntityHandle replacement = entities.create();

    EXPECT_EQ(replacement.index, original.index);
    EXPECT_NE(replacement.generation, original.generation);
    EXPECT_FALSE(entities.is_alive(original));
    EXPECT_TRUE(entities.is_alive(replacement));
}

TEST(GuidTest, GeneratesDistinctCanonicalIdentifiers) {
    const core::Guid first = core::Guid::generate();
    const core::Guid second = core::Guid::generate();

    EXPECT_TRUE(first.valid());
    EXPECT_TRUE(second.valid());
    EXPECT_NE(first, second);
    EXPECT_EQ(first.to_string().size(), 36U);
    EXPECT_EQ(first.to_string()[14], '4');
}

}  // namespace gisengine::tests
