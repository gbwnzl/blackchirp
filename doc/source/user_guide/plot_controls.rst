Plot Controls
=============

All of the plots in Blackchirp share a common set of zoom/pan controls and customization options.
Each plot can be configured individually, and the most recently-used settings should be recalled each time the program is started.
You can configure the appearance of each curve, change which vertical axis a curve is plotted against, control the appearance of a plot grid, and more.

Zooming and Panning
-------------------

Zooming is accomplished by using the mouse wheel.
Scrolling up zooms in, and scrolling down zooms out.
The zoom limits are determined by the X and Y range spanned by the data.
By default, zooming in or out will affect the X axis and both Y axes at the same time.
This behavior can be changed by pressing keys while scrolling:

- ``Ctrl``: Zoom X axis only; Y axes are fixed.
- ``Shift``: Zoom both Y axes, keeping X axis fixed.
- ``Meta`` (Windows key): Zoom left axis only.
- ``Alt``: Zoom right axis only.

To zoom out immediately to the full range of the data, press ``Ctrl`` and left-click the plot.
Alternatively, you can right-click the plot and choose the ``Autoscale`` option.

Panning the plot is accomplished by middle-clicking anywhere on the plot and dragging.
Like with zooming, the panning range is limited to the range of the data displayed on the plot, so panning is not possible when the plot is at full scale.

Plot Configuration Options
--------------------------

.. image:: /_static/user_guide/plot_controls/contextmenu.png
   :width: 400
   :align: center
   :alt: Plot context menu

The right-click context menu contains a variety of customization options to control the appearance and behavior of the plot as a whole.

- ``Autoscale``: resets the X and Y axes to show the full range of the data on the plot.
- ``Wheel Zoom Factor``: The number sets the zooming speed while scrolling the mouse wheel. Larger numbers will zoom by a greater factor per mouse wheel step. The default value for each axis is 0.1.
- ``Tracker``: Enable the tracker to display the coordinates of the mouse cursor on the plot. For each axis, you can configure the number of decimals displayed and, optionally, switch to scientific notation if desired.
- ``Grid``: Configure the appearance of major and minor gridlines. It is possible to control the line style and color for each type of gridline.
- ``Curves``: Options for configuring the appearance of curves on the plot. More details are below.

Additionally, on the Rolling and Aux Data plots, two additional options are available.

- ``Push X Axis``: Set the X scale of all plots to match the selected plot.
- ``Autoscale All``: Apply the autoscale action to all plots.

Curve Configuration Options
---------------------------

As shown in the image above, each curve that is displayed on the plot can be individually configured. Under the ``Curves`` context menu entry, a submenu is available for each curve with the following options:

- ``Export XY``: Generate a csv file containing the currently displayed data.
- ``Color...``: Change the color of the curve.
- ``Line Width``: Change the thickness of the line drawn for the curve.
- ``Line Style``: Change the style (solid, dashed, etc) of the line. Set to "None" if no line is desired.
- ``Marker``: Change the plot marker used for each data point. Set to "None" if no marker is desired.
- ``Marker Size``: Change the size of the marker.
- ``Visible``: Control whether the curve is displayed. If a curve is not visible, it is not included in the autoscale calculation.
- ``Y Axis``: Change which Y axis the curve is plotted on.
- ``Change Plot``: Rolling/Aux Data only. Move the curve to a different plot. The plots are numbered from left to right, then top to bottom.


