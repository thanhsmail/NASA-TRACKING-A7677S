@echo off

set CURDIR=%~dp0
set MODULE=%1
set build_target=%2
set SUB_MODULE=%MODULE%
@REM set option=%3
set ROOTDIR=%CURDIR%

@REM set project_macro=
@REM set IMAGEDIRNAME=


if NOT "%MODULE%"=="" (
	for %%i in (A B C D E F G H I J K L M N O P Q R S T U V W X Y Z) do call set MODULE=%%MODULE:%%i=%%i%%
)
@REM if NOT "%build_target%"=="" (
@REM 	for %%i in (a b c d e f g h i j k l m n o p q r s t u v w x y z) do call set build_target=%%build_target:%%i=%%i%%
@REM )
@REM if NOT "%option%"=="" (
@REM 	for %%i in (a b c d e f g h i j k l m n o p q r s t u v w x y z) do call set option=%%option:%%i=%%i%%
@REM )

echo.
echo ..
echo module=%MODULE%
echo part=%build_target%
@REM echo option=%option%
echo ..

::---------------------default config---------------------------------
@REM set project_macro_FS_OLD=NO_FEATURE_SIMCOM_FS_OLD
@REM set project_macro_OVERSEASGNSS=NO_FEATURE_SIMCOM_GPS_OVERSEAS
@REM set project_macro_AT_SUPPORT=NO_AT_COMMAND_SUPPORT
@REM set project_macro_CUS=CUS_NONE
@REM set project_macro_TLS13=NO_FEATURE_SIMCOM_TLS_1_3

if "%MODULE%"=="MANS" (
	set project_macro_GNSS=FEATURE_SIMCOM_GPS
	goto build
) else if "%MODULE%"=="MANV" (
	set project_macro_GNSS=FEATURE_SIMCOM_GPS
	goto build
) else if "%MODULE%"=="LANS" (
	set project_macro_GNSS=NO_FEATURE_SIMCOM_GPS
	goto build
) else if "%MODULE%"=="LANV" (
	set project_macro_GNSS=NO_FEATURE_SIMCOM_GPS
	goto build
) 

if not "%MODULE%"=="" echo module [%MODULE%] has not exist....

:USAGE

echo.
echo ************************USAGE:**************************
echo *                                                      *
echo *    build.bat ^<module^> ^<part^>                     *
echo *                                                      *
echo * module:                                              *
echo *    MANS                                              *
echo *    MANV                                              *
echo *    LANS                                              *
echo *    LANV                                              *
echo * part:  all/clean/burnApp/burnAll                     *
echo *                                                      *
echo * example:                                             *
echo *    build.bat MANV all                                *
echo *    build.bat MANV clean                              *
echo *    build.bat MANV burn                               *
echo *                                                      *
echo ********************************************************
echo.

exit /b 1


:build

set MODULE=A7677S_%MODULE%_1606_V702_OPENSDK_ENORLFSV2

if "%build_target%" == "all" (
	@call :build_app %MODULE%
) else if "%build_target%" == "clean" (
	@call :clean_image %MODULE%
) else if "%build_target%" == "burnApp" (
	@call :burn_app %MODULE%
) else if "%build_target%" == "burnAll" (
	@call :burn_all %MODULE%
) else (
	if not "%build_target%"=="" echo part [%build_target%] has not exist....
	goto USAGE
)

echo.
echo *********************************************
echo          build %MODULE% success
echo *********************************************
echo.

goto:eof

:build_app
    @call make %MODULE%
	@REM @call C:\Program Files (x86)\GnuWin32\bin\make %MODULE%
	@REM start "generating package for burning app" /d%ROOTDIR% burn_app_a7677s_r2_1606.exe %SUB_MODULE%
	start "generating package for burning app" /d%ROOTDIR% py burn_app_a7677s_r2_1606.py %SUB_MODULE%
goto:eof

:clean_image
    set target_path=%ROOTDIR%out\%MODULE%
    if not exist %target_path% (
	    echo ------------target_path has not exist, the file has been deleted already-------------
	    exit /b 1
    )
	@call make clean_%MODULE%
	@REM @call C:/Program Files (x86)/GnuWin32/bin/make clean_%MODULE%
goto:eof

:burn_all
	set target_module=A7677S_%SUB_MODULE%
	set target_file=SC_%target_module%.zip
    set download_tool=%ROOTDIR%tools\win32\aboot\adownload.exe
    set file_directory=%ROOTDIR%target\%target_module%\%target_file%
	echo file_directory=%file_directory%

    if not exist %file_directory% (
        echo ------------target_file has not exist-------------
        exit /b 1
    )
    @call reset.exe
	@call %download_tool% -u -a -s 3686400 -r -q %file_directory%
goto:eof

:burn_app
	set target_module=A7677S_%SUB_MODULE%
	set target_file=SC_%target_module%_AppOnly.zip
    set download_tool=%ROOTDIR%tools\win32\aboot\adownload.exe
    set file_directory=%ROOTDIR%target\%target_module%\%target_file%
	echo file_directory=%file_directory%

    if not exist %file_directory% (
        echo ------------target_file has not exist-------------
        exit /b 1
    )
    @call reset.exe
	@call %download_tool% -u -a -s 3686400 -r -q %file_directory%
goto:eof

:END

goto:eof


:FAIL

echo.
echo *********************************************
echo          build %MODULE% FAIL
echo *********************************************
echo.

exit /b 1
