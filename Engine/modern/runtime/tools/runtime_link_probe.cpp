#include <cstdlib>

int main()
{
    // The whole-archive probe intentionally has no registered engine client.
    // Skip legacy static-object teardown, which assumes finishGame() exists.
    std::_Exit(0);
}
