function Component()
{
}

Component.prototype.createOperations = function()
{
    component.createOperations();

    try
    {
        if( installer.value("os") === "win" )
        {
            /**
             * Start Menu Shortcut
             */
            component.addOperation( "CreateShortcut", "@TargetDir@\\nheko.exe", "@StartMenuDir@\\nheko.lnk",
                                    "workingDirectory=@TargetDir@", "iconPath=@TargetDir@\\nheko.exe",
                                    "iconId=0", "description=Desktop client for the Matrix protocol");

            /**
             * Desktop Shortcut
             */
            component.addOperation( "CreateShortcut", "@TargetDir@\\nheko.exe", "@DesktopDir@\\nheko.lnk",
                                    "workingDirectory=@TargetDir@", "iconPath=@TargetDir@\\nheko.exe",
                                    "iconId=0", "description=Desktop client for the Matrix protocol");

            /**
             * Cleanup AppData and registry
             */
            if( installer.isUninstaller() )
            {
                component.addElevatedOperation("Execute","echo do nothing","UNDOEXECUTE","cmd /C reg delete HKEY_CURRENT_USER\Software\nheko\nheko\font /f");
                component.addElevatedOperation("Execute","echo do nothing","UNDOEXECUTE","cmd /C reg delete HKEY_CURRENT_USER\Software\nheko\nheko\notifications /f");
                component.addElevatedOperation("Execute","echo do nothing","UNDOEXECUTE","cmd /C reg delete HKEY_CURRENT_USER\Software\nheko\nheko\window /f");
                if( installer.environmentVariable("LOCALAPPDATA") != "" )
                {
                    component.addElevatedOperation("Execute","echo do nothing","UNDOEXECUTE","cmd /C rmdir "%LOCALAPPDATA%\nheko" /f");
                }
            }
        }
    }
    catch( e )
    {
        print( e );
    }
}
