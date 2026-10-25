#include <yaml.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;

static void generated(unsigned count, unsigned order) {
    size_t capacity = (size_t)(count + 1) * 96, used = 0;
    unsigned i, *names = malloc((count + 1) * sizeof(unsigned));
    unsigned *position = malloc((count + 1) * sizeof(unsigned));
    unsigned char *text = malloc(capacity);
    unsigned state = 1234567;
    yaml_parser_t parser;
    yaml_document_t document;
    yaml_node_t *root;
    assert(names && position && text);
    for (i = 0; i < count; ++i) names[i] = order == 1 ? count - i - 1 : i;
    if (order == 2) {
        for (i = count; i > 1; --i) {
            unsigned j, temp;
            state = state * 1664525u + 1013904223u;
            j = state % i;
            temp = names[i - 1]; names[i - 1] = names[j]; names[j] = temp;
        }
    }
    for (i = 0; i < count; ++i) {
        int n = snprintf((char *)text + used, capacity - used,
                "- &a%08u v%u\n", names[i], names[i]);
        assert(n > 0 && (size_t)n < capacity - used); used += (size_t)n;
        position[names[i]] = i + 2;
    }
    for (i = 0; i < count * 2; ++i) {
        int n = snprintf((char *)text + used, capacity - used,
                "- *a%08u\n", i % count);
        assert(n > 0 && (size_t)n < capacity - used); used += (size_t)n;
    }
    assert(yaml_parser_initialize(&parser));
    yaml_parser_set_input_string(&parser, text, used);
    assert(yaml_parser_load(&parser, &document));
    root = yaml_document_get_root_node(&document);
    if (!count) assert(!root);
    else {
        assert(root && root->type == YAML_SEQUENCE_NODE);
        assert(root->data.sequence.items.top - root->data.sequence.items.start == (long)count * 3);
        assert(document.nodes.top - document.nodes.start == (long)count + 1);
        for (i = 0; i < count; ++i) {
            char expected[32];
            yaml_node_t *node = yaml_document_get_node(&document, (int)i + 2);
            snprintf(expected, sizeof(expected), "v%u", names[i]);
            assert(root->data.sequence.items.start[i] == (int)i + 2);
            assert(node && node->type == YAML_SCALAR_NODE);
            assert(strcmp((char *)node->data.scalar.value, expected) == 0);
            assert(root->data.sequence.items.start[count + i] == (int)position[i]);
            assert(root->data.sequence.items.start[count * 2 + i] == (int)position[i]);
        }
    }
    yaml_document_delete(&document);
    yaml_parser_delete(&parser);
    free(text); free(position); free(names); ++checks;
}

static void error_case(const char *input, const char *problem, int duplicate) {
    yaml_parser_t parser;
    yaml_document_t document;
    assert(yaml_parser_initialize(&parser));
    yaml_parser_set_input_string(&parser, (const unsigned char *)input, strlen(input));
    assert(!yaml_parser_load(&parser, &document));
    assert(parser.error == YAML_COMPOSER_ERROR);
    assert(strcmp(parser.problem, problem) == 0);
    if (duplicate) {
        assert(strcmp(parser.context, "found duplicate anchor; first occurrence") == 0);
        assert(parser.context_mark.line == 0 && parser.context_mark.column == 2);
        assert(parser.problem_mark.line == 1 && parser.problem_mark.column == 2);
    } else assert(parser.problem_mark.line == 0 && parser.problem_mark.column == 0);
    yaml_parser_delete(&parser); ++checks;
}

static void self_reference(const char *input, int mapping) {
    yaml_parser_t parser;
    yaml_document_t document;
    yaml_node_t *root;
    assert(yaml_parser_initialize(&parser));
    yaml_parser_set_input_string(&parser, (const unsigned char *)input, strlen(input));
    assert(yaml_parser_load(&parser, &document));
    root = yaml_document_get_root_node(&document); assert(root);
    if (mapping) {
        assert(root->type == YAML_MAPPING_NODE);
        assert(root->data.mapping.pairs.top - root->data.mapping.pairs.start == 1);
        assert(root->data.mapping.pairs.start[0].value == 1);
    } else {
        assert(root->type == YAML_SEQUENCE_NODE);
        assert(root->data.sequence.items.top - root->data.sequence.items.start == 1);
        assert(root->data.sequence.items.start[0] == 1);
    }
    yaml_document_delete(&document); yaml_parser_delete(&parser); ++checks;
}

static void documents(int cross_reference) {
    const char *input = cross_reference ? "--- &a first\n--- *a\n" :
        "--- &a first\n--- &a [*a]\n";
    yaml_parser_t parser;
    yaml_document_t document;
    assert(yaml_parser_initialize(&parser));
    yaml_parser_set_input_string(&parser, (const unsigned char *)input, strlen(input));
    assert(yaml_parser_load(&parser, &document));
    assert(strcmp((char *)yaml_document_get_root_node(&document)->data.scalar.value, "first") == 0);
    yaml_document_delete(&document);
    if (cross_reference) {
        assert(!yaml_parser_load(&parser, &document));
        assert(parser.error == YAML_COMPOSER_ERROR);
        assert(strcmp(parser.problem, "found undefined alias") == 0);
    } else {
        yaml_node_t *root;
        assert(yaml_parser_load(&parser, &document));
        root = yaml_document_get_root_node(&document);
        assert(root->type == YAML_SEQUENCE_NODE && root->data.sequence.items.start[0] == 1);
        yaml_document_delete(&document);
    }
    yaml_parser_delete(&parser); ++checks;
}

static unsigned char *many_anchors(unsigned count, size_t *used, size_t *capacity) {
    unsigned i;
    unsigned char *input;
    *used = 0; *capacity = (count + 8) * 48;
    input = malloc(*capacity); assert(input);
    for (i = 0; i < count; ++i) {
        int n = snprintf((char *)input + *used, *capacity - *used,
                "- &a%08u value\n", i);
        assert(n > 0 && (size_t)n < *capacity - *used); *used += (size_t)n;
    }
    return input;
}

static void indexed_error(unsigned count, int duplicate) {
    size_t used, capacity;
    unsigned char *input = many_anchors(count, &used, &capacity);
    yaml_parser_t parser;
    yaml_document_t document;
    int bytes = duplicate < 0 ?
        snprintf((char *)input + used, capacity - used, "- *missing\n") :
        snprintf((char *)input + used, capacity - used, "- &a%08d again\n", duplicate);
    assert(bytes > 0 && (size_t)bytes < capacity - used); used += (size_t)bytes;
    assert(yaml_parser_initialize(&parser));
    yaml_parser_set_input_string(&parser, input, used);
    assert(!yaml_parser_load(&parser, &document));
    assert(parser.error == YAML_COMPOSER_ERROR);
    assert(parser.problem_mark.line == count && parser.problem_mark.column == 2);
    if (duplicate < 0) assert(strcmp(parser.problem, "found undefined alias") == 0);
    else {
        assert(strcmp(parser.problem, "second occurrence") == 0);
        assert(strcmp(parser.context, "found duplicate anchor; first occurrence") == 0);
        assert(parser.context_mark.line == (size_t)duplicate && parser.context_mark.column == 2);
    }
    assert(document.nodes.start == NULL && parser.aliases.start == NULL);
    yaml_parser_delete(&parser); free(input); ++checks;
}

static void indexed_documents(int invalid_reference) {
    size_t used, capacity;
    unsigned char *input = many_anchors(65, &used, &capacity);
    yaml_parser_t parser;
    yaml_document_t document;
    int bytes = snprintf((char *)input + used, capacity - used, "%s",
            invalid_reference ? "--- *a00000000\n" : "--- &a00000000 second\n");
    assert(bytes > 0 && (size_t)bytes < capacity - used); used += (size_t)bytes;
    assert(yaml_parser_initialize(&parser));
    yaml_parser_set_input_string(&parser, input, used);
    assert(yaml_parser_load(&parser, &document));
    assert(yaml_document_get_root_node(&document)->type == YAML_SEQUENCE_NODE);
    assert(document.nodes.top - document.nodes.start == 66);
    yaml_document_delete(&document);
    if (invalid_reference) {
        assert(!yaml_parser_load(&parser, &document));
        assert(parser.error == YAML_COMPOSER_ERROR);
        assert(strcmp(parser.problem, "found undefined alias") == 0);
    } else {
        yaml_node_t *root;
        assert(yaml_parser_load(&parser, &document));
        root = yaml_document_get_root_node(&document);
        assert(root && root->type == YAML_SCALAR_NODE);
        assert(strcmp((char *)root->data.scalar.value, "second") == 0);
        yaml_document_delete(&document);
    }
    yaml_parser_delete(&parser); free(input); ++checks;
}

int main(void) {
    const unsigned sizes[] = {0,1,7,8,9,15,16,17,63,64,65,127,128,129,255,256,257,1023,1024,1025};
    unsigned i, order;
    for (order = 0; order < 3; ++order)
        for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) generated(sizes[i], order);
    error_case("- &a first\n- &a second\n", "second occurrence", 1);
    error_case("*missing\n", "found undefined alias", 0);
    self_reference("&a [*a]\n", 0);
    self_reference("&a {self: *a}\n", 1);
    documents(0); documents(1);
    indexed_error(65, 0); indexed_error(65, 64); indexed_error(65, -1);
    indexed_error(129, 0); indexed_error(129, 128); indexed_error(129, -1);
    indexed_documents(0); indexed_documents(1);
    printf("{\"public_api_cases\":%u,\"passed\":true}\n", checks);
    return 0;
}
