// usage: ghost_layers_ref bombaz default|custom out.f32   (b11: the 808 reference was removed with the 808 layer)
#include <cstring>
int ref_bombaz(bool custom, const char *out);
int main(int argc, char **argv)
{
    if (argc < 4)
        return 2;
    const bool custom = !std::strcmp(argv[2], "custom");
    return ref_bombaz(custom, argv[3]);
}
