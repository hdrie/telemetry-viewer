//
// Created by Sidney on 27/07/2020.
//

#include <QLegendMarker>
#include "chart_widget.h"
#include "model/telemetry_container.h"
#include "utilities/color.h"
#include "utilities/performance_calculator.h"

void chart_widget::chart_data::detach() const
{
	for(auto &axis : line_series->attachedAxes())
		line_series->detachAxis(axis);
	for(auto &axis : box_series->attachedAxes())
		box_series->detachAxis(axis);
}
void chart_widget::chart_data::hide()
{
	if(!is_hidden)
		return;

	line_series->hide();
	box_series->hide();

	is_hidden = false;
}
void chart_widget::chart_data::show()
{
	if(!is_hidden)
		return;

	line_series->show();
	box_series->show();

	is_hidden = false;
}

void chart_widget::chart_data::update_box_set(int32_t start, int32_t end, double scale_factor) const
{
	performance_calculator calc(*field, start, end);

	const size_t count = calc.get_sample_count();

	if(count < 2)
	{
		box_set->setValue(QBoxSet::LowerExtreme, 0.0);
		box_set->setValue(QBoxSet::UpperExtreme, 0.0);
		box_set->setValue(QBoxSet::Median, 0.0);
		box_set->setValue(QBoxSet::LowerQuartile, 0.0);
		box_set->setValue(QBoxSet::UpperQuartile, 0.0);

		return;
	}

	box_set->setValue(QBoxSet::LowerExtreme, calc.get_minimum() * scale_factor);
	box_set->setValue(QBoxSet::UpperExtreme, calc.get_maximum() * scale_factor);
	box_set->setValue(QBoxSet::Median, calc.get_median_value(0, count) * scale_factor);
	box_set->setValue(QBoxSet::LowerQuartile, calc.get_median_value(0, count / 2) * scale_factor);
	box_set->setValue(QBoxSet::UpperQuartile, calc.get_median_value(count / 2 + (count % 2), count) * scale_factor);
}




chart_widget::chart_widget(QWidget *parent) :
	QChartView(parent),
	m_type(chart_type::line),
	m_memory_scaling(memory_scaling::megabytes),
	m_start(0),
	m_end(std::numeric_limits<int32_t>::max())
{
	m_timeline_axis = new QValueAxis();
	m_timeline_axis->setTitleText("Timeline");
	m_timeline_axis->setTickType(QValueAxis::TicksDynamic);

	m_category_axis = new QBarCategoryAxis();
	m_category_axis->append("Fields");

	build_chart_axis(telemetry_unit::time);
	build_chart_axis(telemetry_unit::value);
	build_chart_axis(telemetry_unit::fps);
	build_chart_axis(telemetry_unit::memory);

	m_line_chart = new QChart();
	m_boxplot_chart = new QChart();

	m_line_chart->addAxis(m_timeline_axis, Qt::AlignBottom);
	m_boxplot_chart->addAxis(m_category_axis, Qt::AlignBottom);

	for(auto &axis : m_axes)
	{
		m_line_chart->addAxis(axis->line_axis, axis->alignment);
		m_boxplot_chart->addAxis(axis->box_axis, axis->alignment);
	}

	switch(m_type)
	{
		case chart_type::line:
			setChart(m_line_chart);
			break;
		case chart_type::boxplot:
			setChart(m_boxplot_chart);
			break;
	}

}
chart_widget::~chart_widget()
{
	for(auto &axis : m_axes)
		delete axis;
}



void chart_widget::set_range(int32_t start, int32_t end)
{
	if(m_start == start && m_end == end)
		return;

	m_start = start;
	m_end = end;

	for(auto &data : m_data)
	{
		auto [ min_value, max_value ] = data.field->get_min_max_data_point_in_range(m_start, m_end);

		data.min_value = min_value;
		data.max_value = max_value;

		double scale_factor = 1.0f;

		if(data.field->unit == telemetry_unit::memory)
			scale_factor = scale_memory(scale_factor);

		data.update_box_set(m_start, m_end, scale_factor);
	}

	rescale_axes();
}

void chart_widget::set_type(chart_type type)
{
	if(m_type == type)
		return;

	m_type = type;

	switch(m_type)
	{
		case chart_type::line:
			setChart(m_line_chart);
			break;
		case chart_type::boxplot:
			setChart(m_boxplot_chart);
			break;
	}

	rescale_axes();
}

void chart_widget::set_memory_scaling(memory_scaling scaling)
{
	if(m_memory_scaling == scaling)
		return;

	m_memory_scaling = scaling;

	QVector<telemetry_provider_field *> fields;

	for(auto &data : m_data)
	{
		if(data.axis->unit != telemetry_unit::memory)
			continue;

		fields.push_back(data.field);
		remove_data(data.field);
	}

	for(auto &field : fields)
		add_data(field);
}




void chart_widget::add_data(telemetry_provider_field *field)
{
	chart_data &data = get_or_create_data_for_field(field);

	if(data.is_hidden)
		data.show();

	rescale_axes();
}
void chart_widget::remove_data(telemetry_provider_field *field)
{
	auto iterator = std::find_if(m_data.begin(), m_data.end(), [&](const chart_data &data) {
		return (field == data.field);
	});

	if(iterator != m_data.end())
	{
		auto &data = *iterator;
		data.detach();

		m_line_chart->removeSeries(data.line_series);
		m_boxplot_chart->removeSeries(data.box_series);

		m_data.erase(iterator);
		rescale_axes();
	}
}

void chart_widget::show_data(telemetry_provider_field *field)
{
	add_data(field);
}
void chart_widget::hide_data(telemetry_provider_field *field)
{
	chart_data &data = get_or_create_data_for_field(field);

	if(!data.is_hidden)
	{
		data.hide();
		rescale_axes();
	}
}

void chart_widget::clear()
{
	for(auto &data : m_data)
		data.detach();

	m_line_chart->removeAllSeries();
	m_boxplot_chart->removeAllSeries();

	m_data.clear();
}



void chart_widget::build_chart_axis(telemetry_unit unit)
{
	auto build_axis = [](telemetry_unit unit) -> QValueAxis * {

		QValueAxis *axis = nullptr;

		switch(unit)
		{
			case telemetry_unit::value:
				axis = new QValueAxis();
				axis->setTitleText("Value");
				break;
			case telemetry_unit::memory:
				axis = new QValueAxis();
				axis->setTitleText("Memory");
				break;
			case telemetry_unit::fps:
				axis = new QValueAxis();
				axis->setTitleText("FPS");
				break;
			case telemetry_unit::time:
				axis = new QValueAxis();
				axis->setTitleText("Time");
				break;

			default:
				break;
		}

		return axis;

	};

	chart_axis *axis = new chart_axis();
	axis->unit = unit;
	axis->range_locked = false;
	axis->visible = false;
	axis->alignment = Qt::AlignLeft;

	if(unit == telemetry_unit::memory || unit == telemetry_unit::fps)
		axis->alignment = Qt::AlignRight;

	axis->line_axis = build_axis(unit);
	axis->box_axis = build_axis(unit);

	m_axes.append(axis);
}

chart_widget::chart_axis *chart_widget::get_chart_axis_for_field(telemetry_provider_field *field) const
{
	telemetry_unit unit = field->unit;
	if(unit == telemetry_unit::duration)
		unit = telemetry_unit::time;

	chart_axis *fallback = nullptr;

	for(auto &axis : m_axes)
	{
		if(axis->unit == unit)
			return axis;

		if(axis->unit == telemetry_unit::value)
			fallback = axis;
	}

	return fallback;
}

chart_widget::chart_data &chart_widget::get_or_create_data_for_field(telemetry_provider_field *field)
{
	auto iterator = std::find_if(m_data.begin(), m_data.end(), [&](const chart_data &data) {
		return (field == data.field);
	});

	if(iterator != m_data.end())
		return *iterator;

	auto [ min_value, max_value ] = field->get_min_max_data_point_in_range(m_start, m_end);

	chart_data &data = m_data.emplace_back();
	data.field = field;
	data.axis = get_chart_axis_for_field(field);
	data.min_value = min_value;
	data.max_value = max_value;

	data.line_series = create_line_series(data.field);

	data.box_set = new QBoxSet();
	data.box_set->setLabel(field->title);
	data.box_set->setBrush(field->color);

	double scale_factor = 1.0f;

	if(data.field->unit == telemetry_unit::memory)
		scale_factor = scale_memory(scale_factor);

	data.update_box_set(m_start, m_end, scale_factor);

	data.box_series = new QBoxPlotSeries();
	data.box_series->setName(field->title);
	data.box_series->append(data.box_set);

	m_line_chart->addSeries(data.line_series);
	m_boxplot_chart->addSeries(data.box_series);

	data.line_series->attachAxis(m_timeline_axis);
	data.line_series->attachAxis(data.axis->line_axis);

	data.box_series->attachAxis(m_category_axis);
	data.box_series->attachAxis(data.axis->box_axis);

	return data;
}


double chart_widget::scale_memory(double bytes) const
{
	switch(m_memory_scaling)
	{
		case memory_scaling::bytes:
			return bytes;
		case memory_scaling::kilobytes:
			return bytes / 1024.0;
		case memory_scaling::megabytes:
			return bytes / 1024.0 / 1024.0;
		case memory_scaling::gigabytes:
			return bytes / 1024.0 / 1024.0 / 1024.0;

	}

	return bytes;
}

void chart_widget::rescale_axes()
{
	uint32_t enabled_fields = 0;

	// Vertical axes
	for(auto &axis : m_axes)
	{
		axis->minimum = std::numeric_limits<double>::max();
		axis->maximum = 0.0;

		axis->visible = false;
	}

	for(auto &data : m_data)
	{
		if(data.is_hidden)
			continue;

		enabled_fields ++;

		data.axis->visible = true;

		data.axis->minimum = std::min(data.axis->minimum, data.min_value.value.toDouble());
		data.axis->maximum = std::max(data.axis->maximum, data.max_value.value.toDouble());
	}

	auto round_up_to_nearest = [](double value, double N) {
		return std::ceil(value / N) * N;
	};

	auto round_down_to_nearest = [](double value, double N) {
		return std::floor(value / N) * N;
	};

	for(auto &axis : m_axes)
	{
		if(axis->line_axis->isVisible() != axis->visible)
			axis->line_axis->setVisible(axis->visible);
		if(axis->box_axis->isVisible() != axis->visible)
			axis->box_axis->setVisible(axis->visible);

		if(!axis->visible)
			continue;

		double min_value = 0.0;
		double max_value = 0.0;

		switch(axis->unit)
		{
			case telemetry_unit::value:
				min_value = round_down_to_nearest(axis->minimum, 1.0);
				max_value = round_up_to_nearest(axis->maximum + 1.0, 1.0);
				break;
			case telemetry_unit::fps:
				min_value = axis->minimum;
				max_value = round_up_to_nearest(axis->maximum, 5.0);
				break;
			case telemetry_unit::time:
				min_value = axis->minimum;
				max_value = std::min(round_up_to_nearest(axis->maximum, 0.01), 0.1);
				break;
			case telemetry_unit::memory:
				min_value = scale_memory(axis->minimum);
				max_value = scale_memory(axis->maximum);
				max_value = 1 << uint32_t(ceil(log2(max_value)));
				break;

			default:
				min_value = axis->minimum;
				max_value = axis->maximum;
				break;
		}

		min_value = std::min(round_down_to_nearest(min_value, 1.0), 0.0);

		axis->line_axis->setRange(min_value, max_value);
		axis->box_axis->setRange(min_value, max_value);
	}

	// Horizontal
	const uint32_t interval = abs(m_end - m_start);

	if(interval >= 5 * 60)
		m_timeline_axis->setTickInterval(60);
	else if(interval >= 50)
		m_timeline_axis->setTickInterval(10);
	else
		m_timeline_axis->setTickInterval(1);

	m_timeline_axis->setRange(m_start, m_end);

	QLegend *legend = m_boxplot_chart->legend();

	for(auto &data : m_data)
	{
		if(!data.is_hidden)
		{
			auto markers = legend->markers(data.box_series);
			markers.first()->setBrush(data.field->color);
		}
	}
}

QLineSeries *chart_widget::create_line_series(telemetry_provider_field *field) const
{
	QLineSeries *series = new QLineSeries();
	series->setColor(field->color);
	series->setName(field->title);

	QPen pen = series->pen();
	pen.setWidth(2);
	series->setPen(pen);

	qreal last_time = -1000.0f;
	qreal last_value = 0.0f;

	for(auto &data : field->data_points)
	{
		// If there is more than a second of time between data changes, repeat the last point again but at the current time
		// this will prevent the graph interpolating between the last and new value, when the telemetry system assumes values are sticky until they change
		if(data.timestamp - last_time >= 1.0)
			series->append(data.timestamp, last_value);

		qreal value;

		switch(field->unit)
		{
			case telemetry_unit::duration:
			{
				QPointF point = data.value.toPointF();
				value = point.y() - point.x();
				break;
			}

			case telemetry_unit::memory:
				value = scale_memory(data.value.toDouble());
				break;

			default:
				value = data.value.toFloat();
				break;
		}

		series->append(data.timestamp, value);

		last_time = data.timestamp;
		last_value = value;
	}

	return series;
}

