/*
 * Copyright (c) 2018-2024, Andreas Kling <andreas@ladybird.org>
 * Copyright (c) 2021, Tobias Christiansen <tobyase@serenityos.org>
 * Copyright (c) 2021-2025, Sam Atkins <sam@ladybird.org>
 * Copyright (c) 2022-2023, MacDue <macdue@dueutil.tech>
 * Copyright (c) 2024, Steffen T. Larssen <dudedbz@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "TransformationStyleValue.h"
#include <AK/StringBuilder.h>
#include <LibWeb/CSS/StyleValues/AngleStyleValue.h>
#include <LibWeb/CSS/StyleValues/CalculatedStyleValue.h>
#include <LibWeb/CSS/StyleValues/LengthStyleValue.h>
#include <LibWeb/CSS/StyleValues/NumberStyleValue.h>
#include <LibWeb/CSS/StyleValues/PercentageStyleValue.h>
#include <LibWeb/Painting/PaintableBox.h>

namespace Web::CSS {

ErrorOr<FloatMatrix4x4> TransformationStyleValue::to_matrix(Optional<Painting::PaintableBox const&> paintable_box) const
{
    auto count = m_properties.values.size();
    auto function_metadata = transform_function_metadata(m_properties.transform_function);

    auto length_to_px = [&](Length const& length) -> ErrorOr<float> {
        if (paintable_box.has_value())
            return length.to_px(paintable_box->layout_node());
        if (length.is_absolute())
            return length.absolute_length_to_px();
        return Error::from_string_literal("Transform contains non absolute units");
    };

    auto get_value = [&](size_t argument_index, CSSPixels const& reference_length = 0) -> ErrorOr<float> {
        auto& transformation_value = *m_properties.values[argument_index];

        if (transformation_value.is_calculated()) {
            auto& calculated = transformation_value.as_calculated();
            switch (function_metadata.parameters[argument_index].type) {
            case TransformFunctionParameterType::Angle: {
                if (!calculated.resolves_to_angle())
                    return Error::from_string_literal("Calculated angle parameter to transform function doesn't resolve to an angle.");
                return calculated.resolve_angle().value().to_radians();
            }
            case TransformFunctionParameterType::Length:
            case TransformFunctionParameterType::LengthNone: {
                if (!calculated.resolves_to_length())
                    return Error::from_string_literal("Calculated length parameter to transform function doesn't resolve to a length.");
                if (!paintable_box.has_value())
                    return Error::from_string_literal("Can't resolve transform-function: Need a paintable box to resolve calculated lengths");
                auto length_resolution_context = Length::ResolutionContext::for_layout_node(paintable_box->layout_node());
                return length_to_px(calculated.resolve_length(length_resolution_context).value());
            }
            case TransformFunctionParameterType::LengthPercentage: {
                if (!calculated.resolves_to_length_percentage())
                    return Error::from_string_literal("Calculated length-percentage parameter to transform function doesn't resolve to a length-percentage.");
                if (!paintable_box.has_value())
                    return Error::from_string_literal("Can't resolve transform-function: Need a paintable box to resolve calculated lengths");
                auto length_resolution_context = Length::ResolutionContext::for_layout_node(paintable_box->layout_node());
                auto length = calculated.resolve_length_percentage(length_resolution_context, Length::make_px(reference_length)).value();
                return length_to_px(length);
            }
            case TransformFunctionParameterType::Number: {
                if (!calculated.resolves_to_number())
                    return Error::from_string_literal("Calculated number parameter to transform function doesn't resolve to a number.");
                return calculated.resolve_number().value();
            }
            case TransformFunctionParameterType::NumberPercentage: {
                if (calculated.resolves_to_number())
                    return calculated.resolve_number().value();
                if (calculated.resolves_to_percentage())
                    return calculated.resolve_percentage().value().as_fraction();
                return Error::from_string_literal("Calculated number/percentage parameter to transform function doesn't resolve to a number or percentage.");
            }
            }
        }

        if (transformation_value.is_length())
            return length_to_px(transformation_value.as_length().length());

        if (transformation_value.is_percentage()) {
            if (function_metadata.parameters[argument_index].type == TransformFunctionParameterType::NumberPercentage) {
                return transformation_value.as_percentage().percentage().as_fraction();
            }
            return length_to_px(Length::make_px(reference_length).percentage_of(transformation_value.as_percentage().percentage()));
        }

        if (transformation_value.is_number())
            return transformation_value.as_number().number();

        if (transformation_value.is_angle())
            return transformation_value.as_angle().angle().to_radians();

        dbgln("FIXME: Unsupported value in transform! {}", transformation_value.to_string(SerializationMode::Normal));
        return Error::from_string_literal("Unsupported value in transform function");
    };

    CSSPixels width = 1;
    CSSPixels height = 1;
    if (paintable_box.has_value()) {
        auto reference_box = paintable_box->transform_box_rect();
        width = reference_box.width();
        height = reference_box.height();
    }

    switch (m_properties.transform_function) {
    case TransformFunction::Perspective:
        // https://drafts.csswg.org/css-transforms-2/#perspective
        // Count is zero when null parameter
        if (count == 1) {
            // FIXME: Add support for the 'perspective-origin' CSS property.
            auto distance = TRY(get_value(0));
            return Gfx::FloatMatrix4x4(1, 0, 0, 0,
                0, 1, 0, 0,
                0, 0, 1, 0,
                0, 0, -1 / (distance <= 0 ? 1 : distance), 1);
        }
        return Gfx::FloatMatrix4x4(1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1);
    case TransformFunction::Matrix:
        if (count == 6)
            return Gfx::FloatMatrix4x4(TRY(get_value(0)), TRY(get_value(2)), 0, TRY(get_value(4)),
                TRY(get_value(1)), TRY(get_value(3)), 0, TRY(get_value(5)),
                0, 0, 1, 0,
                0, 0, 0, 1);
        break;
    case TransformFunction::Matrix3d:
        if (count == 16)
            return Gfx::FloatMatrix4x4(TRY(get_value(0)), TRY(get_value(4)), TRY(get_value(8)), TRY(get_value(12)),
                TRY(get_value(1)), TRY(get_value(5)), TRY(get_value(9)), TRY(get_value(13)),
                TRY(get_value(2)), TRY(get_value(6)), TRY(get_value(10)), TRY(get_value(14)),
                TRY(get_value(3)), TRY(get_value(7)), TRY(get_value(11)), TRY(get_value(15)));
        break;
    case TransformFunction::Translate:
        if (count == 1)
            return Gfx::FloatMatrix4x4(1, 0, 0, TRY(get_value(0, width)),
                0, 1, 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1);
        if (count == 2)
            return Gfx::FloatMatrix4x4(1, 0, 0, TRY(get_value(0, width)),
                0, 1, 0, TRY(get_value(1, height)),
                0, 0, 1, 0,
                0, 0, 0, 1);
        break;
    case TransformFunction::Translate3d:
        return Gfx::FloatMatrix4x4(1, 0, 0, TRY(get_value(0, width)),
            0, 1, 0, TRY(get_value(1, height)),
            0, 0, 1, TRY(get_value(2)),
            0, 0, 0, 1);
        break;
    case TransformFunction::TranslateX:
        if (count == 1)
            return Gfx::FloatMatrix4x4(1, 0, 0, TRY(get_value(0, width)),
                0, 1, 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1);
        break;
    case TransformFunction::TranslateY:
        if (count == 1)
            return Gfx::FloatMatrix4x4(1, 0, 0, 0,
                0, 1, 0, TRY(get_value(0, height)),
                0, 0, 1, 0,
                0, 0, 0, 1);
        break;
    case TransformFunction::TranslateZ:
        if (count == 1)
            return Gfx::FloatMatrix4x4(1, 0, 0, 0,
                0, 1, 0, 0,
                0, 0, 1, TRY(get_value(0)),
                0, 0, 0, 1);
        break;
    case TransformFunction::Scale:
        if (count == 1)
            return Gfx::FloatMatrix4x4(TRY(get_value(0)), 0, 0, 0,
                0, TRY(get_value(0)), 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1);
        if (count == 2)
            return Gfx::FloatMatrix4x4(TRY(get_value(0)), 0, 0, 0,
                0, TRY(get_value(1)), 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1);
        break;
    case TransformFunction::Scale3d:
        if (count == 3)
            return Gfx::FloatMatrix4x4(TRY(get_value(0)), 0, 0, 0,
                0, TRY(get_value(1)), 0, 0,
                0, 0, TRY(get_value(2)), 0,
                0, 0, 0, 1);
        break;
    case TransformFunction::ScaleX:
        if (count == 1)
            return Gfx::FloatMatrix4x4(TRY(get_value(0)), 0, 0, 0,
                0, 1, 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1);
        break;
    case TransformFunction::ScaleY:
        if (count == 1)
            return Gfx::FloatMatrix4x4(1, 0, 0, 0,
                0, TRY(get_value(0)), 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1);
        break;
    case TransformFunction::ScaleZ:
        if (count == 1)
            return Gfx::FloatMatrix4x4(1, 0, 0, 0,
                0, 1, 0, 0,
                0, 0, TRY(get_value(0)), 0,
                0, 0, 0, 1);
        break;
    case TransformFunction::Rotate3d:
        if (count == 4)
            return Gfx::rotation_matrix({ TRY(get_value(0)), TRY(get_value(1)), TRY(get_value(2)) }, TRY(get_value(3)));
        break;
    case TransformFunction::RotateX:
        if (count == 1)
            return Gfx::rotation_matrix({ 1.0f, 0.0f, 0.0f }, TRY(get_value(0)));
        break;
    case TransformFunction::RotateY:
        if (count == 1)
            return Gfx::rotation_matrix({ 0.0f, 1.0f, 0.0f }, TRY(get_value(0)));
        break;
    case TransformFunction::Rotate:
    case TransformFunction::RotateZ:
        if (count == 1)
            return Gfx::rotation_matrix({ 0.0f, 0.0f, 1.0f }, TRY(get_value(0)));
        break;
    case TransformFunction::Skew:
        if (count == 1)
            return Gfx::FloatMatrix4x4(1, tanf(TRY(get_value(0))), 0, 0,
                0, 1, 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1);
        if (count == 2)
            return Gfx::FloatMatrix4x4(1, tanf(TRY(get_value(0))), 0, 0,
                tanf(TRY(get_value(1))), 1, 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1);
        break;
    case TransformFunction::SkewX:
        if (count == 1)
            return Gfx::FloatMatrix4x4(1, tanf(TRY(get_value(0))), 0, 0,
                0, 1, 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1);
        break;
    case TransformFunction::SkewY:
        if (count == 1)
            return Gfx::FloatMatrix4x4(1, 0, 0, 0,
                tanf(TRY(get_value(0))), 1, 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1);
        break;
    }
    dbgln_if(LIBWEB_CSS_DEBUG, "FIXME: Unhandled transformation function {} with {} arguments", CSS::to_string(m_properties.transform_function), m_properties.values.size());
    return Gfx::FloatMatrix4x4::identity();
}

String TransformationStyleValue::to_string(SerializationMode mode) const
{
    // https://drafts.csswg.org/css-transforms-2/#individual-transform-serialization
    if (m_properties.property == PropertyID::Rotate) {
        auto resolve_to_number = [](ValueComparingNonnullRefPtr<CSSStyleValue const> const& value) -> Optional<double> {
            if (value->is_number())
                return value->as_number().number();
            if (value->is_calculated() && value->as_calculated().resolves_to_number())
                return value->as_calculated().resolve_number();

            VERIFY_NOT_REACHED();
        };

        // NOTE: Serialize simple rotations directly.
        switch (m_properties.transform_function) {
            // If the axis is parallel with the x or y axes, it must serialize as the appropriate keyword.
        case TransformFunction::RotateX:
            return MUST(String::formatted("x {}", m_properties.values[0]->to_string(mode)));
        case TransformFunction::RotateY:
            return MUST(String::formatted("y {}", m_properties.values[0]->to_string(mode)));

            // If a rotation about the z axis (that is, in 2D) is specified, the property must serialize as just an <angle>.
        case TransformFunction::Rotate:
        case TransformFunction::RotateZ:
            return m_properties.values[0]->to_string(mode);

        default:
            break;
        }

        auto& rotation_x = m_properties.values[0];
        auto& rotation_y = m_properties.values[1];
        auto& rotation_z = m_properties.values[2];
        auto& angle = m_properties.values[3];

        auto x_value = resolve_to_number(rotation_x).value_or(0);
        auto y_value = resolve_to_number(rotation_y).value_or(0);
        auto z_value = resolve_to_number(rotation_z).value_or(0);

        // If the axis is parallel with the x or y axes, it must serialize as the appropriate keyword.
        if (x_value > 0.0 && y_value == 0 && z_value == 0)
            return MUST(String::formatted("x {}", angle->to_string(mode)));

        if (x_value == 0 && y_value > 0.0 && z_value == 0)
            return MUST(String::formatted("y {}", angle->to_string(mode)));

        // If a rotation about the z axis (that is, in 2D) is specified, the property must serialize as just an <angle>.
        if (x_value == 0 && y_value == 0 && z_value > 0.0)
            return angle->to_string(mode);

        // It must serialize as the keyword none if and only if none was originally specified.
        // NOTE: This is handled by returning a keyword from the parser.

        // If any other rotation is specified, the property must serialize with an axis specified.
        return MUST(String::formatted("{} {} {} {}", rotation_x->to_string(mode), rotation_y->to_string(mode), rotation_z->to_string(mode), angle->to_string(mode)));
    }
    if (m_properties.property == PropertyID::Scale) {
        auto resolve_to_string = [mode](CSSStyleValue const& value) -> String {
            if (value.is_number()) {
                return MUST(String::formatted("{}", value.as_number().number()));
            }
            if (value.is_percentage()) {
                return MUST(String::formatted("{}", value.as_percentage().percentage().as_fraction()));
            }
            return value.to_string(mode);
        };

        auto x_value = resolve_to_string(m_properties.values[0]);
        auto y_value = resolve_to_string(m_properties.values[1]);
        // FIXME: 3D scaling

        StringBuilder builder;
        builder.append(x_value);
        if (x_value != y_value) {
            builder.append(" "sv);
            builder.append(y_value);
        }
        return builder.to_string_without_validation();
    }
    if (m_properties.property == PropertyID::Translate) {
        auto resolve_to_string = [mode](CSSStyleValue const& value) -> Optional<String> {
            if (value.is_length()) {
                if (value.as_length().length().raw_value() == 0)
                    return {};
            }
            if (value.is_percentage()) {
                if (value.as_percentage().percentage().value() == 0)
                    return {};
            }
            return value.to_string(mode);
        };

        auto x_value = resolve_to_string(m_properties.values[0]);
        auto y_value = resolve_to_string(m_properties.values[1]);
        // FIXME: 3D translation

        StringBuilder builder;
        builder.append(x_value.value_or("0px"_string));
        if (y_value.has_value()) {
            builder.append(" "sv);
            builder.append(y_value.value());
        }

        return builder.to_string_without_validation();
    }

    StringBuilder builder;
    builder.append(CSS::to_string(m_properties.transform_function));
    builder.append('(');
    for (size_t i = 0; i < m_properties.values.size(); ++i) {
        auto const& value = m_properties.values[i];

        // https://www.w3.org/TR/css-transforms-2/#individual-transforms
        // A <percentage> is equivalent to a <number>, for example scale: 100% is equivalent to scale: 1.
        // Numbers are used during serialization of specified and computed values.
        if ((m_properties.transform_function == CSS::TransformFunction::Scale
                || m_properties.transform_function == CSS::TransformFunction::Scale3d
                || m_properties.transform_function == CSS::TransformFunction::ScaleX
                || m_properties.transform_function == CSS::TransformFunction::ScaleY
                || m_properties.transform_function == CSS::TransformFunction::ScaleZ)
            && value->is_percentage()) {
            builder.append(String::number(value->as_percentage().percentage().as_fraction()));
        } else {
            builder.append(value->to_string(mode));
        }

        if (i != m_properties.values.size() - 1)
            builder.append(", "sv);
    }
    builder.append(')');

    return MUST(builder.to_string());
}

bool TransformationStyleValue::Properties::operator==(Properties const& other) const
{
    return property == other.property
        && transform_function == other.transform_function
        && values.span() == other.values.span();
}
}
