#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include "utils/logging.h"

int main(int argc, char **argv)
{
	promatte::log::setMinLevel(promatte::log::Level::Warning);
	doctest::Context ctx;
	ctx.applyCommandLine(argc, argv);
	return ctx.run();
}
