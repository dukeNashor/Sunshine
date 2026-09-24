; Migrate an official 64-bit Sunshine MSI before the NSIS install copies files.
; Only an exact LizardByte Sunshine MSI is eligible. Unknown installations are left alone.
SetRegView 64
StrCpy $R0 0
privacy_msi_scan:
  ClearErrors
  EnumRegKey $R1 HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall" $R0
  IfErrors privacy_msi_done
  ReadRegStr $R2 HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\$R1" "DisplayName"
  StrCmp $R2 "Sunshine" 0 privacy_msi_next
  ReadRegStr $R2 HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\$R1" "Publisher"
  StrCmp $R2 "LizardByte" 0 privacy_msi_next
  ClearErrors
  ReadRegDWORD $R2 HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\$R1" "WindowsInstaller"
  IfErrors privacy_msi_next
  IntCmp $R2 1 privacy_msi_uninstall privacy_msi_next privacy_msi_next
privacy_msi_next:
  IntOp $R0 $R0 + 1
  Goto privacy_msi_scan
privacy_msi_uninstall:
  ReadRegStr $R3 HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\$R1" "InstallLocation"
  StrCmp $R3 "" privacy_msi_no_location
  StrCpy $INSTDIR $R3
  DetailPrint "Removing the official Sunshine MSI before installing Privacy Overlay..."
  ClearErrors
  ExecWait 'msiexec.exe /x $R1 /qn /norestart' $R4
  IfErrors privacy_msi_failed
  StrCmp $R4 0 privacy_msi_done
  StrCmp $R4 3010 privacy_msi_reboot
privacy_msi_failed:
  MessageBox MB_ICONSTOP|MB_OK "Official Sunshine MSI removal failed (exit code $R4). Privacy Overlay was not installed." /SD IDOK
  Abort
privacy_msi_no_location:
  MessageBox MB_ICONSTOP|MB_OK "The official Sunshine MSI did not report its installation directory. Privacy Overlay was not installed." /SD IDOK
  Abort
privacy_msi_reboot:
  SetRebootFlag true
privacy_msi_done:
  ; MSI removal can delete its installation directory after CPack's first SetOutPath.
  SetOutPath "$INSTDIR"
