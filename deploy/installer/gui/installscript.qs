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
            component.addOperation( "CreateShortcut", "@TargetDir@\\nheko.exe", "@StartMenuDir@\\nheko.lnk",
                                    "workingDirectory=@TargetDir@", "iconPath=@TargetDir@\\nheko.exe",
                                    "iconId=0", "description=Desktop client for the Matrix protocol");
        }
    }
    catch( e )
    {
        print( e );
    }
}
