#include <mbgl/text/quads.hpp>
#include <mbgl/text/shaping.hpp>
#include <mbgl/geometry/anchor.hpp>
#include <mbgl/style/style_layout.hpp>
#include <mbgl/util/math.hpp>
#include <cassert>

namespace mbgl {

const float globalMinScale = 0.5f; // underscale by 1 zoom level

SymbolQuads getIconQuads(Anchor &anchor, const PositionedIcon &shapedIcon,
        const std::vector<Coordinate> &line, const StyleLayoutSymbol &layout,
        const bool alongLine) {

    vec2<float> tl{shapedIcon.left, shapedIcon.top};
    vec2<float> tr{shapedIcon.right, shapedIcon.top};
    vec2<float> br{shapedIcon.right, shapedIcon.bottom};
    vec2<float> bl{shapedIcon.left, shapedIcon.bottom};


    float angle = layout.icon.rotate * M_PI / 180.0f;
    if (alongLine) {
        assert(static_cast<unsigned int>(anchor.segment) < line.size());
        const Coordinate &next = line[anchor.segment];
        angle += -std::atan2(next.x - anchor.x, next.y - anchor.y) + M_PI / 2;
    }


    if (angle) {
        // Compute the transformation matrix.
        float angle_sin = std::sin(angle);
        float angle_cos = std::cos(angle);
        std::array<float, 4> matrix = {{angle_cos, -angle_sin, angle_sin, angle_cos}};

        tl = tl.matMul(matrix);
        tr = tr.matMul(matrix);
        bl = bl.matMul(matrix);
        br = br.matMul(matrix);
    }

    SymbolQuads quads;
    quads.emplace_back(tl, tr, bl, br, shapedIcon.image, 0, anchor, globalMinScale, std::numeric_limits<float>::infinity());
    return quads;
}

struct GlyphInstance {
    explicit GlyphInstance(const vec2<float> &anchor_) : anchor(anchor_) {}
    explicit GlyphInstance(const vec2<float> &anchor_, float offset_, float minScale_, float maxScale_,
            float angle_)
        : anchor(anchor_), offset(offset_), minScale(minScale_), maxScale(maxScale_), angle(angle_) {}

    const vec2<float> anchor;
    const float offset = 0.0f;
    const float minScale = globalMinScale;
    const float maxScale = std::numeric_limits<float>::infinity();
    const float angle = 0.0f;
};

typedef std::vector<GlyphInstance> GlyphInstances;

void getSegmentGlyphs(std::back_insert_iterator<GlyphInstances> glyphs, Anchor &anchor,
        float offset, const std::vector<Coordinate> &line, int segment, int8_t direction) {

    const bool upsideDown = direction < 0;

    if (offset < 0)
        direction *= -1;

    if (direction > 0)
        segment++;

    assert((int)line.size() > segment);
    vec2<float> end = line[segment];
    vec2<float> newAnchor = anchor;
    float prevscale = std::numeric_limits<float>::infinity();

    offset = std::fabs(offset);

    const float placementScale = anchor.scale;

    while (true) {
        const float dist = util::dist<float>(newAnchor, end);
        const float scale = offset / dist;
        float angle = -std::atan2(end.x - newAnchor.x, end.y - newAnchor.y) + direction * M_PI / 2.0f;
        if (upsideDown)
            angle += M_PI;

        glyphs = GlyphInstance{
            /* anchor */ newAnchor,
            /* offset */ static_cast<float>(upsideDown ? M_PI : 0.0),
            /* minScale */ scale,
            /* maxScale */ prevscale,
            /* angle */ static_cast<float>(std::fmod((angle + 2.0 * M_PI), (2.0 * M_PI)))};

        if (scale <= placementScale)
            break;

        newAnchor = end;

        // skip duplicate nodes
        while (newAnchor == end) {
            segment += direction;
            if ((int)line.size() <= segment || segment < 0) {
                anchor.scale = scale;
                return;
            }
            end = line[segment];
        }

        vec2<float> normal = util::normal<float>(newAnchor, end) * dist;
        newAnchor = newAnchor - normal;

        prevscale = scale;
    }
}

SymbolQuads getGlyphQuads(Anchor &anchor, const Shaping &shapedText, 
        const float boxScale, const std::vector<Coordinate> &line, const StyleLayoutSymbol &layout,
        const bool alongLine, const GlyphPositions &face) {

    const float textRotate = layout.text.rotate * M_PI / 180;
    const bool keepUpright = layout.text.keep_upright;

    SymbolQuads quads;

    for (const PositionedGlyph &positionedGlyph: shapedText.positionedGlyphs) {
        auto face_it = face.find(positionedGlyph.glyph);
        if (face_it == face.end())
            continue;
        const Glyph &glyph = face_it->second;
        const Rect<uint16_t> &rect = glyph.rect;

        if (!glyph)
            continue;

        if (!rect)
            continue;

        const float centerX = (positionedGlyph.x + glyph.metrics.advance / 2.0f) * boxScale;

        GlyphInstances glyphInstances;
        if (alongLine) {
            getSegmentGlyphs(std::back_inserter(glyphInstances), anchor, centerX, line, anchor.segment, 1);
            if (keepUpright)
                getSegmentGlyphs(std::back_inserter(glyphInstances), anchor, centerX, line, anchor.segment, -1);

        } else {
            glyphInstances.emplace_back(GlyphInstance{anchor});
        }

        // The rects have an addditional buffer that is not included in their size;
        const float rectBuffer = 3.0f;

        const float x1 = positionedGlyph.x + glyph.metrics.left - rectBuffer;
        const float y1 = positionedGlyph.y - glyph.metrics.top - rectBuffer;
        const float x2 = x1 + rect.w;
        const float y2 = y1 + rect.h;

        const vec2<float> otl{x1, y1};
        const vec2<float> otr{x2, y1};
        const vec2<float> obl{x1, y2};
        const vec2<float> obr{x2, y2};

        for (const GlyphInstance &instance : glyphInstances) {

            vec2<float> tl = otl;
            vec2<float> tr = otr;
            vec2<float> bl = obl;
            vec2<float> br = obr;
            const float angle = instance.angle + textRotate;

            if (angle) {
                // Compute the transformation matrix.
                float angle_sin = std::sin(angle);
                float angle_cos = std::cos(angle);
                std::array<float, 4> matrix = {{angle_cos, -angle_sin, angle_sin, angle_cos}};

                tl = tl.matMul(matrix);
                tr = tr.matMul(matrix);
                bl = bl.matMul(matrix);
                br = br.matMul(matrix);
            }

            // Prevent label from extending past the end of the line
            const float glyphMinScale = std::max(instance.minScale, anchor.scale);

            const float glyphAngle = std::fmod((anchor.angle + textRotate + instance.offset + 2 * M_PI), (2 * M_PI));
            quads.emplace_back(tl, tr, bl, br, rect, glyphAngle, instance.anchor, glyphMinScale, instance.maxScale);

        }

    }

    return quads;
}
}
