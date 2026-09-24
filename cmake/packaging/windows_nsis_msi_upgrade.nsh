; Migrate an official 64-bit Sunshine MSI before the NSIS install copies files.
; Only an exact LizardByte Sunshine MSI is eligible. Unknown installations are left alone.
SetRegView 64
StrCpy $R0 0
privacy_msi_scan:
  ClearErrors
  EnumRegKey $R1 HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall" $R0
  IfErrors privacy_msi_done
  ; EnumRegKey returns an empty name, without setting the error flag, at the end.
  StrCmp $R1 "" privacy_msi_done
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
  ; Stage configuration outside the MSI-owned directory before uninstalling it.
  StrCpy $R5 ""
  GetTempFileName $R5
  IfErrors privacy_msi_backup_failed
  Delete "$R5"
  ClearErrors
  CreateDirectory "$R5"
  IfErrors privacy_msi_backup_failed
  IfFileExists "$INSTDIR\config\*.*" 0 privacy_msi_backup_apps
  ClearErrors
  CopyFiles /SILENT "$INSTDIR\config" "$R5"
  IfErrors privacy_msi_backup_failed
privacy_msi_backup_apps:
  IfFileExists "$INSTDIR\apps.json" 0 privacy_msi_backup_conf
  ClearErrors
  CopyFiles /SILENT "$INSTDIR\apps.json" "$R5"
  IfErrors privacy_msi_backup_failed
privacy_msi_backup_conf:
  IfFileExists "$INSTDIR\sunshine.conf" 0 privacy_msi_backup_state
  ClearErrors
  CopyFiles /SILENT "$INSTDIR\sunshine.conf" "$R5"
  IfErrors privacy_msi_backup_failed
privacy_msi_backup_state:
  IfFileExists "$INSTDIR\sunshine_state.json" 0 privacy_msi_backup_credentials
  ClearErrors
  CopyFiles /SILENT "$INSTDIR\sunshine_state.json" "$R5"
  IfErrors privacy_msi_backup_failed
privacy_msi_backup_credentials:
  IfFileExists "$INSTDIR\credentials\*.*" 0 privacy_msi_backup_covers
  ClearErrors
  CopyFiles /SILENT "$INSTDIR\credentials" "$R5"
  IfErrors privacy_msi_backup_failed
privacy_msi_backup_covers:
  IfFileExists "$INSTDIR\covers\*.*" 0 privacy_msi_run_uninstall
  ClearErrors
  CopyFiles /SILENT "$INSTDIR\covers" "$R5"
  IfErrors privacy_msi_backup_failed
privacy_msi_run_uninstall:
  DetailPrint "Removing the official Sunshine MSI before installing Privacy Overlay..."
  ClearErrors
  ExecWait 'msiexec.exe /x $R1 /qn /norestart' $R4
  IfErrors privacy_msi_failed
  StrCmp $R4 0 privacy_msi_restore
  StrCmp $R4 3010 privacy_msi_reboot
privacy_msi_failed:
  MessageBox MB_ICONSTOP|MB_OK "Official Sunshine MSI removal failed (exit code $R4). Privacy Overlay was not installed. Configuration copy: $R5" /SD IDOK
  Abort
privacy_msi_no_location:
  MessageBox MB_ICONSTOP|MB_OK "The official Sunshine MSI did not report its installation directory. Privacy Overlay was not installed." /SD IDOK
  Abort
privacy_msi_backup_failed:
  StrCmp $R5 "" privacy_msi_backup_error
  ; R5 is a temporary path returned by GetTempFileName, never registry input.
  Delete "$R5"
  RMDir /r "$R5"
privacy_msi_backup_error:
  MessageBox MB_ICONSTOP|MB_OK "Could not preserve the existing Sunshine configuration. The official MSI was not removed. Check the temporary path: $R5" /SD IDOK
  Abort
privacy_msi_reboot:
  SetRebootFlag true
  Goto privacy_msi_restore
privacy_msi_restore:
  SetOutPath "$INSTDIR"
  IfFileExists "$R5\config\*.*" 0 privacy_msi_restore_apps
  ClearErrors
  CopyFiles /SILENT "$R5\config" "$INSTDIR"
  IfErrors privacy_msi_restore_failed
privacy_msi_restore_apps:
  IfFileExists "$R5\apps.json" 0 privacy_msi_restore_conf
  ClearErrors
  CopyFiles /SILENT "$R5\apps.json" "$INSTDIR"
  IfErrors privacy_msi_restore_failed
privacy_msi_restore_conf:
  IfFileExists "$R5\sunshine.conf" 0 privacy_msi_restore_state
  ClearErrors
  CopyFiles /SILENT "$R5\sunshine.conf" "$INSTDIR"
  IfErrors privacy_msi_restore_failed
privacy_msi_restore_state:
  IfFileExists "$R5\sunshine_state.json" 0 privacy_msi_restore_credentials
  ClearErrors
  CopyFiles /SILENT "$R5\sunshine_state.json" "$INSTDIR"
  IfErrors privacy_msi_restore_failed
privacy_msi_restore_credentials:
  IfFileExists "$R5\credentials\*.*" 0 privacy_msi_restore_covers
  ClearErrors
  CopyFiles /SILENT "$R5\credentials" "$INSTDIR"
  IfErrors privacy_msi_restore_failed
privacy_msi_restore_covers:
  IfFileExists "$R5\covers\*.*" 0 privacy_msi_restore_done
  ClearErrors
  CopyFiles /SILENT "$R5\covers" "$INSTDIR"
  IfErrors privacy_msi_restore_failed
privacy_msi_restore_done:
  ; R5 is an isolated directory created from GetTempFileName.
  RMDir /r "$R5"
  Goto privacy_msi_done
privacy_msi_restore_failed:
  MessageBox MB_ICONSTOP|MB_OK "Could not restore the existing Sunshine configuration. Privacy Overlay was not installed. Configuration copy: $R5" /SD IDOK
  Abort
privacy_msi_done:
  ; MSI removal can delete its installation directory after CPack's first SetOutPath.
  SetOutPath "$INSTDIR"
