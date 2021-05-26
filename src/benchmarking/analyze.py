import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

data = pd.read_csv('../../benchmarking/client_0.csv')

data[" diff_usec"].mean()

data[" diff_usec"].plot()