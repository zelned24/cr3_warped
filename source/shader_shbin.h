/* Shader binary produced by picasso from source/shader.v.pica.
 * The build converts build/shader.shbin into an object file with bin2s,
 * which exports the symbols declared here. */
#ifndef SHADER_SHBIN_H
#define SHADER_SHBIN_H

#include <stddef.h>
#include <stdint.h>

extern const uint8_t shader_shbin[];
extern const uint8_t shader_shbin_end[];

#define shader_shbin_size ((uint32_t)(shader_shbin_end - shader_shbin))

#endif
