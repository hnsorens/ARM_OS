
#include "helpers.h"

void PrintNumber(EFI_SYSTEM_TABLE *SystemTable, UINT64 number) {
  CHAR16 buffer[64];
  INTN i = 0;

  // Handle zero
  if (number == 0) {
    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"0");
    return;
  }

  // Convert to string backwards
  while (number > 0) {
    buffer[i++] = L'0' + (number % 10);
    number /= 10;
  }

  // Print in correct order
  while (i > 0) {
    CHAR16 digit[2] = {buffer[--i], 0};
    SystemTable->ConOut->OutputString(SystemTable->ConOut, digit);
  }
}
