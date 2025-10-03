# Efficient Office Lighting
My IoT project developed for the Internet of Things course. 
It's an IoT application that takes place in an office: every desk lamp is an IoT node that asks the machine learning node what brightness level should have in order to keep a comfortable lighting for the worker. Each lamp has a lighting sensor that sense the current ambient lightining, measured in lux. The sampled ambient lux value is then sent as a CoAP request to the ml node (along with the desired brightness level) that answers with the brightness level the lamp should have.
The ML node runs a simple Decision Tree Regression model, suitable for constrained devices, extracted using TinyML.
