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
             * Cleanup AppData and registry
             */
            component.addElevatedOperation("Execute","echo do nothing","UNDOEXECUTE","cmd /C reg delete HKEY_CURRENT_USER\Software\nheko\nheko /f");
            if( installer.environmentVariable("LOCALAPPDATA") != "" )
            {
                component.addElevatedOperation("Execute","echo do nothing","UNDOEXECUTE","cmd /C rmdir %LOCALAPPDATA%\nheko /f");
            }
        }
    }
    catch( e )
    {
        print( e );
    }
}
