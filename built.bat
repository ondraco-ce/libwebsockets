MAIN_FOLDER=%~dp0

@echo ****
@echo **** XV300
@echo ****

cd %MAIN_FOLDER%
cmake -E make_directory "bin/XV300"
cd "bin/XV300"

cmake -G"Visual Studio 9 2008 XV300 (ARMv7)" ../../ ^
-DLWS_WITHOUT_DEMONIZE=ON ^
-DLWS_WITH_ZLIB=ON ^
-DLWS_WITH_BUNDLED_ZLIB=OFF ^
-DLWS_ZLIB_INCLUDE_DIRS="C:\Users\E9916442\Galileo_Code\trunk\runtime\external\zlib\xv300\include" ^
-DLWS_ZLIB_LIBRARIES="C:\Users\E9916442\Galileo_Code\trunk\runtime\external\zlib\xv300\lib\zlibstaticd-mt.lib" ^
-DLWS_WITH_STATIC=ON ^
-DLWS_WITH_SHARED=OFF ^
-DLWS_WITHOUT_TESTAPPS=ON ^
-DLWS_OPENSSL_INCLUDE_DIRS="C:/Users/E9916442/Galileo_Code/OPEN_SSL/include/openssl" ^
-DLWS_OPENSSL_LIBRARIES="C:\Users\E9916442\Galileo_Code\OPEN_SSL\lib\libcrypto.lib;C:\Users\E9916442\Galileo_Code\OPEN_SSL\lib\libssl.lib" ^
-DOPENSSL_EXECUTABLE="C:\Users\E9916442\Galileo_Code\OPEN_SSL\bin\openssl.exe"
::@echo ****
::@echo **** Win32
::@echo ****
::
::cd %MAIN_FOLDER%
::cmake -E make_directory "bin/Win32"
::cd "bin/Win32"
::
::cmake -G"Visual Studio 9 2008" ../../

pause