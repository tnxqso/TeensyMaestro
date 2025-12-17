FLASHMEM void storage_configure()
{
#if 0
  Serial.println("[MTP] storage_configure: starting MTP_Teensy");
#endif

  MTP.begin();

  if (!TM_SD_Ensure()) {
    Serial.println("[MTP] TM_SD_Ensure() failed, MTP disabled");
    return;
  }

  MTP.addFilesystem(SD, "BUILTIN-SD");
}
