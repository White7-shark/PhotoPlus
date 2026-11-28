# Toon Studio's finer controls ("More controls"): English and hand-translated into the app's 8 other languages.
# Run: python3 tools/i18n/content/toon_controls.py (merges into src/i18n/strings/*.json)
import json, os

LANGS = ["en", "es", "pt-BR", "fr", "ar", "hi", "id", "ru", "zh-CN"]
S = {
 "toon_more": ["More controls", "Más controles", "Mais controles", "Plus de réglages", "عناصر تحكم إضافية", "और नियंत्रण", "Kontrol lainnya", "Дополнительные настройки", "更多控制"],
 "toon_outline": ["Outline", "Contorno", "Contorno", "Contour", "الخط الخارجي", "आउटलाइन", "Garis tepi", "Контур", "外轮廓"],
 "toon_backdrop": ["Background softness", "Suavidad del fondo", "Suavidade do fundo", "Douceur de l’arrière-plan", "نعومة الخلفية", "बैकग्राउंड की कोमलता", "Kelembutan latar", "Мягкость фона", "背景柔和度"],
 "toon_brightness": ["Brightness", "Brillo", "Brilho", "Luminosité", "السطوع", "चमक", "Kecerahan", "Яркость", "亮度"],
 "toon_warmth": ["Warmth", "Calidez", "Calor", "Chaleur", "الدفء", "गर्माहट", "Kehangatan", "Теплота", "色温"],
 "toon_texture": ["Texture (dots, tone, grain)", "Textura (puntos, tramas, grano)", "Textura (pontos, retícula, granulado)", "Texture (points, trame, grain)", "الملمس (نقاط، ظلال، حبيبات)", "टेक्सचर (डॉट, टोन, ग्रेन)", "Tekstur (titik, tone, butiran)", "Текстура (точки, тон, зерно)", "质感（网点、网纹、颗粒）"],
 "toon_face_detail": ["Face lines", "Líneas del rostro", "Linhas do rosto", "Traits du visage", "خطوط الوجه", "चेहरे की रेखाएं", "Garis wajah", "Линии лица", "面部线条"],
 "toon_hair_detail": ["Hair detail", "Detalle del cabello", "Detalhe do cabelo", "Détail des cheveux", "تفاصيل الشعر", "बालों की डिटेल", "Detail rambut", "Детали волос", "头发细节"],
 "toon_ink": ["Ink colour", "Color de la tinta", "Cor da tinta", "Couleur de l’encre", "لون الحبر", "इंक का रंग", "Warna tinta", "Цвет туши", "墨线颜色"],
 "toon_ink_style": ["Style’s own", "Del estilo", "Do estilo", "Du style", "لون الأسلوب", "स्टाइल का", "Bawaan gaya", "Как в стиле", "风格默认"],
 "toon_ink_black": ["Black", "Negra", "Preta", "Noire", "أسود", "काला", "Hitam", "Чёрная", "黑色"],
 "toon_ink_brown": ["Brown", "Marrón", "Marrom", "Brune", "بني", "भूरा", "Cokelat", "Коричневая", "棕色"],
 "toon_ink_colour": ["Colour", "De color", "Colorida", "Colorée", "ملوّن", "रंगीन", "Berwarna", "Цветная", "彩色"],
}
root = os.path.join(os.path.dirname(__file__), '../../../src/i18n/strings')
tables = {l: json.load(open(os.path.join(root, f'{l}.json'))) for l in LANGS}
for key, vals in S.items():
    assert len(vals) == len(LANGS), key
    for l, v in zip(LANGS, vals):
        tables[l][key] = v
for l in LANGS:
    p = os.path.join(root, f'{l}.json')
    json.dump(tables[l], open(p, 'w'), ensure_ascii=False, indent=2)
    open(p, 'a').write('\n')
print(len(S), 'strings in', len(LANGS), 'languages')
