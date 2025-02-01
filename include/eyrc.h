typedef struct { // :String
    char const* cont;
    int32_t len;
} tech_sozonov_eyr_String;

int32_t
tech_sozonov_eyr_compileFile(tech_sozonov_eyr_String filename);

tech_sozonov_eyr_String
tech_sozonov_eyr_compile(tech_sozonov_eyr_String sourceCode);
