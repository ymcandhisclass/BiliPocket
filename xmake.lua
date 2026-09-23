add_rules('mode.release', 'mode.debug', 'mode.releasedbg')

set_languages('cxx17', 'c11')
set_warnings('all')
set_exceptions('cxx')

if is_mode('releasedbg') then
    set_symbols('debug')
    set_optimize('fast')
end

target('bili_plugin')
    set_kind('shared')
    add_rules('qt.shared')

    add_files('src/*.cpp')
    add_files('src/modules/**/*.cpp')
    add_files('src/*.h')
    add_files('src/modules/**/*.h')
    add_includedirs('src')

    add_frameworks(
        'QtCore',
        'QtQuick',
        'QtQml',
        'QtNetwork',
        'QtMultimedia',
        'QtGui'
    )

    -- 显式添加 Qt5 Multimedia 头文件路径（xmake 的 add_frameworks 对 Qt5 Multimedia 支持不完善）
    if is_plat('linux') then
        if is_arch('x86_64') then
            add_includedirs('/usr/include/x86_64-linux-gnu/qt5/QtMultimedia')
        elseif is_arch('arm64', 'aarch64') then
            add_includedirs('/usr/include/aarch64-linux-gnu/qt5/QtMultimedia')
        end
    end
