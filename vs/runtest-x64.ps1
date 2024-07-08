$ODG = [convert]::ToDouble(( `
  Invoke-Expression ("$Env:GSTREAMER_1_0_ROOT_MSVC_X86_64\bin\gst-launch-1.0.exe --gst-plugin-load=$PSScriptRoot\x64\Release\gstpeaq.dll " + `
         'audiotestsrc name=src0 num-buffers=128 freq=440 ' + `
         'tee ' + `
         'queue name=queue0 ' + `
         'queue name=queue1 ' + `
         'peaq name=peaq0 ' + `
         'src0.src!tee0.sink ' + `
         'tee0.src_2!queue0.sink ' + `
         'tee0.src_1!queue1.sink ' + `
         'queue0.src!peaq0.ref ' + `
         'queue1.src!peaq0.test') `
      | Select-String -Pattern 'Objective Difference Grade: (.*)').Matches.Groups[1].Value)
if ($ODG -ne 0.171) {
    throw "$ODG -ne 0.171"
}

$ODG = [convert]::ToDouble(( `
  Invoke-Expression ("$Env:GSTREAMER_1_0_ROOT_MSVC_X86_64\bin\gst-launch-1.0.exe --gst-plugin-load=$PSScriptRoot\x64\Release\gstpeaq.dll " + `
	'audiotestsrc name=src0 num-buffers=128 wave=saw freq=440 ' + `
	'audiotestsrc name=src1 num-buffers=128 wave=triangle freq=440 ' + `
	'peaq name=peaq ' + `
	'src0.src!peaq.ref src1.src!peaq.test')`
 | Select-String -Pattern 'Objective Difference Grade: (.*)').Matches.Groups[1].Value)
if ($ODG -ne -2.007) {
    throw "$ODG -ne -2.007"
}

$ODG = [convert]::ToDouble(( `
  Invoke-Expression ("$Env:GSTREAMER_1_0_ROOT_MSVC_X86_64\bin\gst-launch-1.0.exe --gst-plugin-load=$PSScriptRoot\x64\Release\gstpeaq.dll " + `
	'audiotestsrc name=src0 num-buffers=128 wave=saw freq=440 ' + `
	'audiotestsrc name=src1 num-buffers=128 wave=triangle freq=440 ' + `
	'peaq name=peaq ' + `
	'src0.src!audio/x-raw,channels=2!peaq.ref src1.src!peaq.test') `
 | Select-String -Pattern 'Objective Difference Grade: (.*)').Matches.Groups[1].Value)
if ($ODG -ne -2.007) {
    throw "$ODG -ne -2.007"
}

$ODG = [convert]::ToDouble(( `
  Invoke-Expression ("$Env:GSTREAMER_1_0_ROOT_MSVC_X86_64\bin\gst-launch-1.0.exe --gst-plugin-load=$PSScriptRoot\x64\Release\gstpeaq.dll " + `
	'audiotestsrc name=src0 num-buffers=128 wave=saw freq=440 ' + `
	'audiotestsrc name=src1 num-buffers=128 wave=triangle freq=440 ' + `
	'peaq name=peaq ' + `
	'src0.src!peaq.ref src1.src!audio/x-raw,channels=2!peaq.test') `
 | Select-String -Pattern 'Objective Difference Grade: (.*)').Matches.Groups[1].Value)
if ($ODG -ne -2.007) {
    throw "$ODG -ne -2.007"
}

$ODG = [convert]::ToDouble(( `
  Invoke-Expression ("$PSScriptRoot\x64\Release\peaq.exe " + `
	"$PSScriptRoot\..\test\stimulus_ref.flac $PSScriptRoot\..\test\stimulus_test.wav") `
 | Select-String -Pattern 'Objective Difference Grade: (.*)').Matches.Groups[1].Value)
if ($ODG -ne -1.077) {
    throw "$ODG -ne -1.077"
}
$ODG = [convert]::ToDouble(( `
  Invoke-Expression ("$PSScriptRoot\x64\Release\peaq.exe " + `
	"$PSScriptRoot\..\test\stimulus_test.wav $PSScriptRoot\..\test\stimulus_ref.flac ") `
 | Select-String -Pattern 'Objective Difference Grade: (.*)').Matches.Groups[1].Value)
if ($ODG -ne -3.096) {
    throw "$ODG -ne -3.096"
}
