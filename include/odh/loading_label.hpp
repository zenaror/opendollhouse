// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace odh_host {
inline constexpr std::string_view loading_label(std::uint8_t language) {
    constexpr std::array<std::string_view,20> labels{
        "Loading...","Loading...","Chargement...","Lädt...","Caricamento...",
        "Cargando...","Laden...","Indlæser...","Laddar...","Laster...",
        "Ladataan...","טוען...","Загрузка...","Carregando...","読み込み中...",
        "Wczytywanie...","正在加载...","正在載入...","กำลังโหลด...","불러오는 중..."};
    return language>=1&&language<=labels.size()?labels[language-1]:labels[0];
}
}
