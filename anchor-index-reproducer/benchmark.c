#define _POSIX_C_SOURCE 200809L
#include <yaml.h>
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void scalar(yaml_document_t *document, int index, const char *expected) {
    yaml_node_t *node = yaml_document_get_node(document, index);
    assert(node && node->type == YAML_SCALAR_NODE);
    assert(strcmp((char *)node->data.scalar.value, expected) == 0);
}

static void load(const unsigned char *input, size_t length, unsigned kind,
        unsigned count, int verify) {
    yaml_parser_t parser;
    yaml_document_t document;
    assert(yaml_parser_initialize(&parser));
    yaml_parser_set_input_string(&parser, input, length);
    assert(yaml_parser_load(&parser, &document));
    if (verify) {
        yaml_node_t *root = yaml_document_get_root_node(&document);
        if (kind == 0) assert(!root);
        else if (kind == 4) {
            const char *keys[] = {"service", "replicas", "ports"};
            yaml_node_t *ports;
            unsigned i;
            assert(root && root->type == YAML_MAPPING_NODE);
            assert(root->data.mapping.pairs.top - root->data.mapping.pairs.start == 3);
            for (i = 0; i < 3; ++i)
                scalar(&document, root->data.mapping.pairs.start[i].key, keys[i]);
            scalar(&document, root->data.mapping.pairs.start[0].value, "worker");
            scalar(&document, root->data.mapping.pairs.start[1].value, "3");
            ports = yaml_document_get_node(&document, root->data.mapping.pairs.start[2].value);
            assert(ports && ports->type == YAML_SEQUENCE_NODE);
            assert(ports->data.sequence.items.top - ports->data.sequence.items.start == 2);
            scalar(&document, ports->data.sequence.items.start[0], "80");
            scalar(&document, ports->data.sequence.items.start[1], "443");
        } else {
            unsigned i;
            assert(root && root->type == YAML_SEQUENCE_NODE);
            assert(root->data.sequence.items.top - root->data.sequence.items.start ==
                    (long)count * (kind == 3 ? 2 : 1));
            assert(document.nodes.top - document.nodes.start == (long)count + 1);
            for (i = 0; i < count; ++i) {
                char expected[32];
                snprintf(expected, sizeof(expected), "v%u", i);
                assert(root->data.sequence.items.start[i] == (int)i + 2);
                scalar(&document, (int)i + 2, expected);
                if (kind == 3)
                    assert(root->data.sequence.items.start[count + i] == (int)count + 1);
            }
        }
    }
    yaml_document_delete(&document);
    assert(yaml_parser_load(&parser, &document));
    assert(!yaml_document_get_root_node(&document));
    yaml_document_delete(&document);
    yaml_parser_delete(&parser);
}

static unsigned long peak_rss(void) {
    FILE *stream = fopen("/proc/self/status", "r");
    char line[256];
    unsigned long value = 0;
    if (!stream) return 0;
    while (fgets(line, sizeof(line), stream))
        if (sscanf(line, "VmHWM: %lu kB", &value) == 1) break;
    fclose(stream);
    return value;
}

int main(int argc, char **argv) {
    const char *mapping = "service: worker\nreplicas: 3\nports: [80, 443]\n";
    unsigned kind, count, rounds, i, warmup;
    size_t capacity, used = 0;
    unsigned char *input;
    uint64_t digest = UINT64_C(14695981039346656037);
    struct timespec begin, end;
    clock_t cpu_begin, cpu_end;
    double wall;
    assert(argc == 4);
    kind = (unsigned)strtoul(argv[1], NULL, 10);
    count = (unsigned)strtoul(argv[2], NULL, 10);
    rounds = (unsigned)strtoul(argv[3], NULL, 10);
    assert(kind <= 4 && count <= 16000 && rounds > 0 && rounds <= 50000);
    assert((kind == 0 || kind == 4) ? count == 0 : count > 0);
    capacity = (size_t)(count + 1) * 64 + 128;
    input = malloc(capacity); assert(input);
    if (kind == 4) { used = strlen(mapping); memcpy(input, mapping, used); }
    else if (kind != 0) {
        for (i = 0; i < count; ++i) {
            int n = kind == 1 ?
                snprintf((char *)input + used, capacity - used, "- v%u\n", i) :
                snprintf((char *)input + used, capacity - used, "- &a%08u v%u\n", i, i);
            assert(n > 0 && (size_t)n < capacity - used); used += (size_t)n;
        }
        if (kind == 3) {
            for (i = 0; i < count; ++i) {
                int n = snprintf((char *)input + used, capacity - used, "- *a%08u\n", count - 1);
                assert(n > 0 && (size_t)n < capacity - used); used += (size_t)n;
            }
        }
    }
    for (i = 0; i < used; ++i) { digest ^= input[i]; digest *= UINT64_C(1099511628211); }
    load(input, used, kind, count, 1);
    warmup = count >= 1024 ? 1 : 3;
    for (i = 0; i < warmup; ++i) load(input, used, kind, count, 0);
    assert(clock_gettime(CLOCK_MONOTONIC, &begin) == 0); cpu_begin = clock();
    for (i = 0; i < rounds; ++i) load(input, used, kind, count, 0);
    cpu_end = clock(); assert(clock_gettime(CLOCK_MONOTONIC, &end) == 0);
    wall = end.tv_sec - begin.tv_sec + (end.tv_nsec - begin.tv_nsec) / 1000000000.0;
    printf("{\"kind\":%u,\"count\":%u,\"rounds\":%u,\"input_bytes\":%zu,"
           "\"input_fnv64\":\"%016" PRIx64 "\",\"full_preflight_verified\":true,"
           "\"wall_seconds\":%.9f,\"cpu_seconds\":%.9f,\"peak_rss_kib\":%lu}\n",
           kind, count, rounds, used, digest, wall,
           (double)(cpu_end - cpu_begin) / CLOCKS_PER_SEC, peak_rss());
    free(input);
    return 0;
}
