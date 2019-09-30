
#ifndef SCARD_H_
#define SCARD_H_

bool scard_create_context();
void scard_destroy_context();
void scard_detect_reader();
bool scard_reader_presence();
char *scard_reader_name();
void scard_probe_for_card();
bool scard_card_presence();
bool scard_card_identify();

#endif // SCARD_H_
